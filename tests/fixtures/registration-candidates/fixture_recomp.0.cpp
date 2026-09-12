// Synthetic instruction-comment fixture, not compiled game code.
DEFINE_REX_FUNC(tail_registration) {
    // lwz r11,0(r3)
    // mr r6,r3
    // lwz r11,56(r11)
    // mtctr r11
    // bctr
}
DEFINE_REX_FUNC(call_registration) {
    // lwz r10,60(r11)
    // mr r3,r31
    // mtctr r10
    // bctrl
}
DEFINE_REX_FUNC(overwritten_target) {
    // lwz r11,60(r3)
    // lwz r11,4(r10)
    // mtctr r11
    // bctrl
}
DEFINE_REX_FUNC(replaced_ctr) {
    // lwz r11,56(r3)
    // mtctr r11
    // mtctr r10
    // bctrl
}
DEFINE_REX_FUNC(branch_boundary) {
    // lwz r11,56(r3)
    // beq cr6,0x82000000
    // mtctr r11
    // bctrl
}
DEFINE_REX_FUNC(no_cross_function) {
    // lwz r11,56(r3)
    // mtctr r11
}
DEFINE_REX_FUNC(unrelated_function) {
    // bctrl
}
DEFINE_REX_FUNC(target_changed_after_ctr) {
    // lwz r11,56(r3)
    // mtctr r11
    // li r11,0
    // bctrl
}
