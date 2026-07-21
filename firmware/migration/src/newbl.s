/* Embeds the new bootloader binary (copied to newbl.bin by the Makefile) into
 * the bl_updater's .bl_image section. bl_image_start/end bound it. */
    .section .bl_image, "a", %progbits
    .global bl_image_start
    .global bl_image_end
    .align 2
bl_image_start:
    .incbin "newbl.bin"
    .align 2
bl_image_end:
