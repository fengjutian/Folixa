# 批量加密（AES-256 + 权限位）。
#
# 仓库根用 CMAKE_CURRENT_LIST_DIR/../.. 绑：片段文件本身在 tests/cmake/ 下，
# 这样无论谁 include、从哪个目录 configure，路径都对，不写死绝对路径。
#
# 为什么整组都要 qpdf：加密本身走 qpdf 的 R6 安全处理器，
# 没有 qpdf 就没法产出一份「用密码能打开」的 PDF，这条能力也就无从验证。
# 所以这组测试与 PdfSecurityTests 一样，只在带 qpdf 的构建里存在。
#
# 为什么还要 PDFium：加密后的文件必须能被真的打开（读回验证），
# 而且「不给密码打不开」这件事要用另一个实现验，不能只信加密那一步自己说。
#
# 依赖的变量（PDF_FIXTURE_DIR、pdf-fixtures 目标、AIPDF_PDFIUM_LIBRARY、
# AIPDF_PDFIUM_INCLUDE_DIR）与函数（aipdf_deploy_qt_runtime、aipdf_deploy_pdfium）
# 都在父作用域里定义好了，include 进来直接可用。

get_filename_component(AIPDF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if (AIPDF_WITH_QPDF AND qpdf_FOUND)
    qt_add_executable(BatchEncryptTests
        ${CMAKE_CURRENT_LIST_DIR}/../BatchEncryptTests.cpp
        ${AIPDF_REPO_ROOT}/src/doc/batch/BatchProcessor.cpp
        ${AIPDF_REPO_ROOT}/src/doc/batch/BatchProcessor.h
        ${AIPDF_REPO_ROOT}/src/pdf/security/PdfSecurity.cpp
        ${AIPDF_REPO_ROOT}/src/pdf/security/PdfSecurity.h
        ${AIPDF_REPO_ROOT}/src/pdf/document/PdfRevisionCounter.cpp
        ${AIPDF_REPO_ROOT}/src/pdf/document/PdfRevisionCounter.h
        ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.cpp
        ${AIPDF_REPO_ROOT}/src/pdf/kernel/PdfiumAdapter.h
        ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.cpp
        ${AIPDF_REPO_ROOT}/src/pdf/annot/Annotation.h
        ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.cpp
        ${AIPDF_REPO_ROOT}/src/pdf/kernel/TextPage.h
    )

    target_link_libraries(BatchEncryptTests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Test
        ${AIPDF_PDFIUM_LIBRARY}
        qpdf::libqpdf
    )

    target_include_directories(BatchEncryptTests PRIVATE
        ${AIPDF_REPO_ROOT}/src
        ${AIPDF_PDFIUM_INCLUDE_DIR}
    )

    target_compile_definitions(BatchEncryptTests PRIVATE AIPDF_WITH_QPDF=1)

    set_target_properties(BatchEncryptTests PROPERTIES WIN32_EXECUTABLE FALSE)

    # 部署 Qt 运行时与 PDFium 的函数定义在仓库根作用域里；
    # 独立的自检工程不一定有它们，所以先问一句再用。
    if (COMMAND aipdf_deploy_qt_runtime)
        aipdf_deploy_qt_runtime(BatchEncryptTests)
    endif()
    if (COMMAND aipdf_deploy_pdfium)
        aipdf_deploy_pdfium(BatchEncryptTests)
    endif()
    add_custom_command(TARGET BatchEncryptTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:qpdf::libqpdf>" "$<TARGET_FILE_DIR:BatchEncryptTests>"
        COMMENT "Copying qpdf runtime for BatchEncryptTests"
    )

    add_test(NAME BatchEncryptTests COMMAND BatchEncryptTests)
endif()
