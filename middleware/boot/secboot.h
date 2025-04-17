#ifndef __SECBOOT_H__
#define __SECBOOT_H__

#ifdef __cplusplus
extern "C" {
#endif


void dfu_boot_img_in_flash(int flashid);

void boot_set_flash_read_func(flash_read_func read_func);

#ifdef __cplusplus
}
#endif


#endif /* __SECBOOT_H__ */

