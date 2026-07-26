#!/bin/bash

function lf_rm() {
    for file in "$@"; do
        if [ -e "$file" ]; then
            echo "Deleting: $file"
            rm -rf "$file"  
        #else
            #echo "File not found: $file" 
        fi
    done
}

# remove unused files
function remove_data()
{
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/*.aiisp
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/*.data
    
    # drm ( sample program required )
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/libdrm*
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/libdrm_rockchip*

    # kms
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/libkms*

    # freetype
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/libfreetype*

    # iconv
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/libiconv*

    # rkAVS
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/librkAVS*
    
    # jpeg
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/libjpeg*

    # png
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/lib/libpng*

    # vqefiles
    lf_rm $RK_PROJECT_PACKAGE_OEM_DIR/usr/share/vqefiles/*


    /*暂时去掉不需要的进程，这里关闭启动脚本*/
    INITD_DIR="${SDK_ROOT_DIR}/output/out/rootfs_uclibc_rv1106/etc/init.d"

    [ -f "${INITD_DIR}/S21appinit" ] && mv "${INITD_DIR}/S21appinit" "${INITD_DIR}/K21appinit"
    [ -f "${INITD_DIR}/S50sshd" ] && mv "${INITD_DIR}/S50sshd" "${INITD_DIR}/K50sshd"
    [ -f "${INITD_DIR}/S60micinit" ] && mv "${INITD_DIR}/S60micinit" "${INITD_DIR}/K60micinit"
    [ -f "${INITD_DIR}/S99python" ] && mv "${INITD_DIR}/S99python" "${INITD_DIR}/K99python"
    [ -f "${INITD_DIR}/S99_auto_reboot" ] && mv "${INITD_DIR}/S99_auto_reboot" "${INITD_DIR}/K99_auto_reboot"
}

#=========================
# run
#=========================
remove_data