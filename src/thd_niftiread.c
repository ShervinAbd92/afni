#include "mrilib.h"

#ifndef DONT_INCLUDE_ANALYZE_STRUCT
#define DONT_INCLUDE_ANALYZE_STRUCT
#endif
#include "nifti1_io.h"   /** will include nifti1.h **/

/*******************************************************************/
/********** 26 Aug 2003: read a NIFTI-1 file as a dataset **********/
/*******************************************************************/

THD_3dim_dataset * THD_open_nifti( char *pathname )
{
   THD_3dim_dataset *dset=NULL ;
   nifti_image *nim ;
   int ntt , nbuc , nvals ;
   int statcode = 0 , datum , iview , ibr ;
   THD_ivec3 orixyz , nxyz ;
   THD_fvec3 dxyz , orgxyz ;
   THD_mat33 R ;
   char *ppp , prefix[THD_MAX_PREFIX] ;

ENTRY("THD_open_nifti") ;

   /*-- open input file --*/

   { /* set the nifti_io debug level       8 Apr 2005 [rickr] */
      char * ept = my_getenv("AFNI_NIFTI_DEBUG");
      if( ept != NULL ) nifti_set_debug_level(atoi(ept));
   }

   nim = nifti_image_read( pathname, 0 ) ;

   if( nim == NULL || nim->nifti_type == 0 ) RETURN(NULL) ;

   /*-- extract some useful AFNI-ish information from the nim struct --*/

   /* we must have at least 2 spatial dimensions */

   if( nim->nx < 2 || nim->ny < 2 ) RETURN(NULL) ;

   /* 4th dimension = time; 5th dimension = bucket:
      these are mutually exclusive in AFNI at present */

   ntt = nim->nt ; nbuc = nim->nu ;
   if( ntt > 1 && nbuc > 1 ){
     fprintf(stderr,
             "** AFNI can't deal with 5 dimensional NIfTI dataset file %s\n",
             pathname ) ;
     RETURN(NULL) ;
   }
   nvals = MAX(ntt,nbuc) ;

   /* determine type of dataset values:
      if we are scaling, or if the data type in the NIfTI file
      is something AFNI can't handle, then the result will be floats */

   switch( nim->datatype ){
     default:
       fprintf(stderr,
               "** AFNI can't handle NIFTI datatype=%d (%s) in file %s\n",
               nim->datatype, nifti_datatype_string(nim->datatype), pathname );
       RETURN(NULL) ;
     break ;

     case DT_UINT8:     datum = (nim->scl_slope != 0.0) ? MRI_float : MRI_byte  ; break ;
     case DT_INT16:     datum = (nim->scl_slope != 0.0) ? MRI_float : MRI_short ; break ;

     case DT_FLOAT32:   datum = MRI_float   ; break ;
     case DT_COMPLEX64: datum = MRI_complex ; break ;
     case DT_RGB24:     datum = MRI_rgb     ; break ;

     case DT_INT8:      /* NIfTI-1 data types that AFNI can't handle directly */
     case DT_UINT16:
     case DT_INT32:
     case DT_UINT32:
     case DT_FLOAT64:
       fprintf(stderr,
              "** AFNI converts NIFTI_datatype=%d (%s) in file %s to FLOAT32\n",
              nim->datatype, nifti_datatype_string(nim->datatype), pathname );
       datum = MRI_float ;
     break ;

#if 0
     case DT_COMPLEX128:  /* this case would be too much like real work */
       fprintf(stderr,
               "** AFNI convert NIFTI_datatype=%d (%s) in file %s to COMPLEX64\n",
               nim->datatype, nifti_datatype_string(nim->datatype), pathname );
       datum = MRI_complex ;
     break ;
#endif
   }

   /* check for statistics code */

   if( nim->intent_code >= NIFTI_FIRST_STATCODE &&
       nim->intent_code <= NIFTI_LAST_STATCODE    ){

     if( nim->intent_code > FUNC_PT_TYPE ){
       fprintf(stderr,
               "** AFNI doesn't understand NIFTI statistic type %d (%s) in file %s\n",
               nim->intent_code , nifti_intent_string(nim->intent_code) , pathname ) ;
     } else {
       statcode = nim->intent_code ;
       if( nbuc > 1 ){
         fprintf(stderr,
                 "** AFNI doesn't support NIFTI voxel-dependent statistic parameters"
                 " in file %s\n" , pathname ) ;
         statcode = 0 ;
       }
     }
   }

   /* determine orientation from the qto_xyz matrix,
      which transforms (i,j,k) voxel indexes to (x,y,z) LPI coordinates */

   LOAD_MAT(R, -nim->qto_xyz.m[0][0] ,  /* negate x and y   */
               -nim->qto_xyz.m[0][1] ,  /* coefficients,    */
               -nim->qto_xyz.m[0][2] ,  /* since AFNI works */
               -nim->qto_xyz.m[1][0] ,  /* with RAI coords, */
               -nim->qto_xyz.m[1][1] ,  /* but NIFTI uses   */
               -nim->qto_xyz.m[1][2] ,  /* LPI coordinates. */
                nim->qto_xyz.m[2][0] ,  /* [Which is my own] */
                nim->qto_xyz.m[2][1] ,  /* [damn fault!!!!!] */
                nim->qto_xyz.m[2][2]  ) ;

   orixyz = THD_matrix_to_orientation( R ) ;   /* compute orientation codes */

   /* load the offsets and the grid spacings */

   LOAD_FVEC3( orgxyz , -nim->qto_xyz.m[0][3] ,    /* again, negate  */
                        -nim->qto_xyz.m[1][3] ,    /* x and y coords */
                         nim->qto_xyz.m[2][3]  ) ;

   /* AFNI space units are always mm */

   if( nim->xyz_units == NIFTI_UNITS_METER ){
     nim->dx *= 1000.0 ; nim->dy *= 1000.0 ; nim->dz *= 1000.0 ;
   } else if(  nim->xyz_units == NIFTI_UNITS_MICRON ){
     nim->dx *= 0.001  ; nim->dy *= 0.001  ; nim->dz *= 0.001  ;
   }

   LOAD_FVEC3( dxyz , (ORIENT_sign[orixyz.ijk[0]]=='+') ? nim->dx : -nim->dx ,
                      (ORIENT_sign[orixyz.ijk[1]]=='+') ? nim->dy : -nim->dy ,
                      (ORIENT_sign[orixyz.ijk[2]]=='+') ? nim->dz : -nim->dz  ) ;

   /*-- make an AFNI dataset! --*/

   dset = EDIT_empty_copy(NULL) ;

   ppp  = THD_trailname(pathname,0) ;               /* strip directory */
   MCW_strncpy( prefix , ppp , THD_MAX_PREFIX ) ;   /* to make prefix */

   nxyz.ijk[0] = nim->nx ;                          /* grid dimensions */
   nxyz.ijk[1] = nim->ny ;
   nxyz.ijk[2] = nim->nz ;

   iview = (nim->qform_code == NIFTI_XFORM_TALAIRACH )
           ? VIEW_TALAIRACH_TYPE : VIEW_ORIGINAL_TYPE ;

   dset->idcode.str[0] = 'N' ;  /* overwrite 1st 3 bytes with something special */
   dset->idcode.str[1] = 'I' ;
   dset->idcode.str[2] = 'I' ;

   MCW_hash_idcode( pathname , dset ) ;  /* 06 May 2005 */

   EDIT_dset_items( dset ,
                      ADN_prefix      , prefix ,
                      ADN_datum_all   , datum ,
                      ADN_nxyz        , nxyz ,
                      ADN_xyzdel      , dxyz ,
                      ADN_xyzorg      , orgxyz ,
                      ADN_xyzorient   , orixyz ,
                      ADN_malloc_type , DATABLOCK_MEM_MALLOC ,
                      ADN_view_type   , iview ,
                      ADN_type        , (statcode != 0) ? HEAD_FUNC_TYPE
                                                        : HEAD_ANAT_TYPE ,
                    ADN_none ) ;

   /* not a time dependent dataset */

   if( ntt < 2 ){
     EDIT_dset_items( dset ,
                        ADN_nvals     , nbuc ,
                        ADN_datum_all , datum ,
                        ADN_func_type , (statcode != 0) ? FUNC_BUCK_TYPE
                                                        : ANAT_BUCK_TYPE ,
                      ADN_none ) ;

   } else {  /* is a time dependent dataset */

          if( nim->time_units == NIFTI_UNITS_MSEC ) nim->dt *= 0.001 ;
     else if( nim->time_units == NIFTI_UNITS_USEC ) nim->dt *= 1.e-6 ;
     EDIT_dset_items( dset ,
                        ADN_nvals     , ntt ,
                        ADN_ntt       , ntt ,
                        ADN_datum_all , datum ,
                        ADN_ttorg     , 0.0 ,
                        ADN_ttdel     , nim->dt ,
                        ADN_ttdur     , 0.0 ,
                        ADN_tunits    , UNITS_SEC_TYPE ,
                        ADN_func_type , (statcode != 0) ? FUNC_FIM_TYPE
                                                        : ANAT_EPI_TYPE ,
                      ADN_none ) ;

     /* if present, add stuff about the slice-timing offsets */

     if( nim->slice_dim      == 3               && /* AFNI can only deal with */
         nim->slice_code     >  0               && /* slice timing offsets    */
         nim->slice_duration >  0.0             && /* along the k-axis of     */
         nim->slice_start    >= 0               && /* the dataset volume      */
         nim->slice_start    < nim->nz          &&
         nim->slice_end      > nim->slice_start &&
         nim->slice_end      < nim->nz             ){

       float *toff=(float *)calloc(sizeof(float),nim->nz) , tsl ;
       int kk ;

            if( nim->time_units == NIFTI_UNITS_MSEC ) nim->slice_duration *= 0.001;
       else if( nim->time_units == NIFTI_UNITS_USEC ) nim->slice_duration *= 1.e-6;

       /* set up slice time offsets in the divers orders */

       switch( nim->slice_code ){
         case NIFTI_SLICE_SEQ_INC:
           tsl = 0.0 ;
           for( kk=nim->slice_start ; kk <= nim->slice_end ; kk++ ){
             toff[kk] = tsl ; tsl += nim->slice_duration ;
           }
         break ;
         case NIFTI_SLICE_SEQ_DEC:
           tsl = 0.0 ;
           for( kk=nim->slice_end ; kk >= nim->slice_end ; kk-- ){
             toff[kk] = tsl ; tsl += nim->slice_duration ;
           }
         break ;
         case NIFTI_SLICE_ALT_INC:
           tsl = 0.0 ;
           for( kk=nim->slice_start ; kk <= nim->slice_end ; kk+=2 ){
             toff[kk] = tsl ; tsl += nim->slice_duration ;
           }
           for( kk=nim->slice_start+1 ; kk <= nim->slice_end ; kk+=2 ){
             toff[kk] = tsl ; tsl += nim->slice_duration ;
           }
         break ;
         case NIFTI_SLICE_ALT_DEC:
           tsl = 0.0 ;
           for( kk=nim->slice_end ; kk >= nim->slice_start ; kk-=2 ){
             toff[kk] = tsl ; tsl += nim->slice_duration ;
           }
           for( kk=nim->slice_end-1 ; kk >= nim->slice_start ; kk-=2 ){
             toff[kk] = tsl ; tsl += nim->slice_duration ;
           }
         break ;
       }

       EDIT_dset_items( dset ,
                          ADN_nsl     , nim->nz       ,
                          ADN_zorg_sl , orgxyz.xyz[2] ,
                          ADN_dz_sl   , dxyz.xyz[2]   ,
                          ADN_toff_sl , toff          ,
                        ADN_none ) ;

       free(toff) ;

     } /* end of slice timing stuff */

   } /* end of 3D+time dataset stuff */

   /* add statistics, if present */

   if( statcode != 0 ){
     for( ibr=0 ; ibr < nvals ; ibr++ )
       EDIT_STATAUX4(dset,ibr,statcode,nim->intent_p1,nim->intent_p2,nim->intent_p3,0) ;
   }

   /*-- flag to read data from disk using NIFTI functions --*/

   dset->dblk->diskptr->storage_mode = STORAGE_BY_NIFTI ;
   strcpy( dset->dblk->diskptr->brick_name , pathname ) ;
   dset->dblk->diskptr->byte_order = nim->byteorder ;

#if 0
   for( ibr=0 ; ibr < nvals ; ibr++ ){     /* make sub-brick labels */
     sprintf(prefix,"%s[%d]",tname,ibr) ;
     EDIT_BRICK_LABEL( dset , ibr , prefix ) ;
   }
#endif

   /* return unpopulated dataset */

   nifti_image_free(nim) ; RETURN(dset) ;
}

/*-----------------------------------------------------------------
  Load a NIFTI dataset's data into memory
  (called from THD_load_datablock in thd_loaddblk.c)
    - RWC: modified 07 Apr 2005 to read data bricks via nifti_io.c
      'NBL' functions, rather than directly from disk
-------------------------------------------------------------------*/

void THD_load_nifti( THD_datablock *dblk )
{
   THD_diskptr *dkptr ;
   int nx,ny,nz,nxy,nxyz,nxyzv , nerr=0,ibr,nv, nslice ;
   int datum, need_copy=0 ;
   void *ptr ;
   nifti_image *nim ;
   nifti_brick_list NBL ;  /* holds the data read from disk */

ENTRY("THD_load_nifti") ;

   /*-- open and read input [these errors should never occur] --*/

   if( !ISVALID_DATABLOCK(dblk)                        ||
       dblk->diskptr->storage_mode != STORAGE_BY_NIFTI ||
       dblk->brick == NULL                               ) EXRETURN ;

   dkptr = dblk->diskptr ;

   STATUS("calling nifti_image_read_bricks") ;
   NBL.nbricks = 0 ;
   nim = nifti_image_read_bricks( dkptr->brick_name, 0,NULL , &NBL ) ;
   if( nim == NULL || NBL.nbricks <= 0 ) EXRETURN ;

   datum = DBLK_BRICK_TYPE(dblk,0) ;  /* destination data type */

   /*-- determine if we need to copy the data from the
        bricks as loaded above because of a type conversion --*/

   switch( nim->datatype ){
     case DT_INT16:
     case DT_UINT8:      need_copy = (datum == MRI_float) ; break ;

     case DT_FLOAT32:
     case DT_COMPLEX64:
     case DT_RGB24:      need_copy = 0 ; break ;

     case DT_INT8:       /* these are the cases where AFNI can't */
     case DT_UINT16:     /* directly handle the NIFTI datatype,  */
     case DT_INT32:      /* so we'll convert them to floats.     */
     case DT_UINT32:
     case DT_FLOAT64:    need_copy = 1 ; break ;
#if 0
     case DT_COMPLEX128: need_copy = 1 ; break ;
#endif
   }

   /*-- various dimensions --*/

   nx = dkptr->dimsizes[0] ;
   ny = dkptr->dimsizes[1] ;  nxy   = nx * ny   ;
   nz = dkptr->dimsizes[2] ;  nxyz  = nxy * nz  ;
   nv = dkptr->nvals       ;  if( nv > NBL.nbricks ) nv = NBL.nbricks ;
   nxyzv = nxyz * nv ; nslice = nz*nv ;

   dblk->malloc_type = DATABLOCK_MEM_MALLOC ;

   /*------ don't need to copy data ==> just copy pointers from NBL ------*/

   if( !need_copy ){

     STATUS("copying brick pointers directly") ;
     for( ibr=0 ; ibr < nv ; ibr++ ){
       mri_fix_data_pointer( NBL.bricks[ibr] ,DBLK_BRICK(dblk,ibr) ) ;
       NBL.bricks[ibr] = NULL ;  /* so it won't be deleted later */

       if( AFNI_yesenv("AFNI_FLOATSCAN") ){  /*--- check float inputs? ---*/
         if( DBLK_BRICK_TYPE(dblk,ibr) == MRI_float )
           nerr += thd_floatscan( DBLK_BRICK_NVOX(dblk,ibr) ,
                                  DBLK_ARRAY(dblk,ibr)        ) ;
         else if( DBLK_BRICK_TYPE(dblk,ibr) == MRI_complex )
           nerr += thd_complexscan( DBLK_BRICK_NVOX(dblk,ibr) ,
                                    DBLK_ARRAY(dblk,ibr)        ) ;
       }
     }
     if( nerr > 0 ) fprintf(stderr ,
                    "** %s: found %d float errors -- see program float_scan\n",
                    dkptr->brick_name , nerr ) ;

   } else { /*---------- need to copy data ==> do some more work -----------*/

     register int ii ; void *nbuf ;

     STATUS("converting input bricks to floats") ;
     for( ibr=0 ; ibr < nv ; ibr++ ){

       if( DBLK_ARRAY(dblk,ibr) == NULL ){                     /* make space */
         ptr = AFMALL(void, DBLK_BRICK_BYTES(dblk,ibr) ) ;     /* for this   */
         mri_fix_data_pointer( ptr ,  DBLK_BRICK(dblk,ibr) ) ; /* sub-brick! */
       }
       ptr = DBLK_ARRAY(dblk,ibr) ; if( ptr == NULL ) break ;  /* bad news!! */

       nbuf = NBL.bricks[ibr] ;              /* data as read from NIfTI file */

       /* macro to convert data from type "ityp" in nbuf to float in dataset */

#undef  CPF
#define CPF(ityp) do{ ityp *sar = (ityp *)nbuf ; float *far = (float *)ptr ;   \
                      for( ii=0 ; ii < nxyz ; ii++ ) far[ii] = (float)sar[ii]; \
                  } while(0)

       /* load from nbuf into brick array (will be float or complex) */

       switch( nim->datatype ){
         case DT_UINT8:    CPF(unsigned char)  ; break ;
         case DT_INT8:     CPF(signed char)    ; break ;
         case DT_INT16:    CPF(signed short)   ; break ;
         case DT_UINT16:   CPF(unsigned short) ; break ;
         case DT_INT32:    CPF(signed int)     ; break ;
         case DT_UINT32:   CPF(unsigned int)   ; break ;
         case DT_FLOAT64:  CPF(double)         ; break ;
#if 0
         case DT_COMPLEX128: break ;
#endif
       }

       free(NBL.bricks[ibr]) ; NBL.bricks[ibr] = NULL ;
     }
   }

   nifti_free_NBL( &NBL ) ;  /* done with this */

   /*-- scale results? ---*/

   if( nim->scl_slope != 0.0 ){       /*--- scale results? ---*/
     STATUS("scaling sub-bricks") ;
     for( ibr=0 ; ibr < nv ; ibr++ ){
       if( DBLK_BRICK_TYPE(dblk,ibr) == MRI_float ){
         float *far = (float *) DBLK_ARRAY(dblk,ibr) ; int ii ;
         for( ii=0 ; ii < nxyz ; ii++ )
           far[ii] = nim->scl_slope * far[ii] + nim->scl_inter ;
       } else if( DBLK_BRICK_TYPE(dblk,ibr) == MRI_complex ){
         complex *car = (complex *) DBLK_ARRAY(dblk,ibr) ; int ii ;
         for( ii=0 ; ii < nxyz ; ii++ ){
           car[ii].r = nim->scl_slope * car[ii].r + nim->scl_inter ;
           car[ii].i = nim->scl_slope * car[ii].i + nim->scl_inter ;
         }
       }
     }
   }

   /*-- throw away the trash and return --*/

   nifti_image_free(nim) ; EXRETURN ;
}
