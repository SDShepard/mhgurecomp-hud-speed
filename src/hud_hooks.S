    .text
    .arm
    .fpu neon
    .hidden hud_gui_prepare
    .hidden hud_gui_target
    .global hud_gui_entry
    .hidden hud_gui_entry
    .type hud_gui_entry,%function
hud_gui_entry:
    // The game uses s0 for GUI delta despite the plugin's softfp C ABI.
    // Save all volatile integer/VFP registers before calling any C helper.
    push {r0-r12,lr}
    vpush {d0-d7}
    vpush {d16-d31}
    sub sp,sp,#24
    mrs r2,APSR
    str r2,[sp,#16]
    vmrs r2,fpscr
    str r2,[sp,#20]
    mov r1,sp
    bl hud_gui_prepare
    // Reproduce the original normalization entry's observable scratch outputs.
    ldr r2,[sp,#0]
    str r2,[sp,#220]
    ldr r2,[sp,#4]
    str r2,[sp,#152]
    ldr r2,[sp,#8]
    str r2,[sp,#160]
    ldr r2,[sp,#12]
    str r2,[sp,#168]
    ldr r2,[sp,#20]
    vmsr fpscr,r2
    ldr r2,[sp,#16]
    msr APSR_nzcvqg,r2
    add sp,sp,#24
    vpop {d16-d31}
    vpop {d0-d7}
    pop {r0-r12,lr}
    // Tail enter the unmodified scheduler. Keep the caller's r12 and lr too.
    push {r12,lr}
    ldr r12,.Ltarget
.Ltarget_pc:
    add r12,pc,r12
    ldr r12,[r12]
    str r12,[sp,#4]
    pop {r12,pc}
    .align 2
.Ltarget:
    .word hud_gui_target-(.Ltarget_pc+8)
    .size hud_gui_entry,.-hud_gui_entry
