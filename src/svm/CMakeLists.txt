set(SVM_SRCS 3dsvm_common.c svm_common.c svm_learn.c svm_hideo.c)

add_executable(3dsvm ${SVM_SRCS} 3dsvm.c)


target_link_libraries(3dsvm PRIVATE AFNI::mri)
