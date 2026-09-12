# Used only after the enclosing retail function has passed its full SHA gate.
# The known bank arguments and dirty-mask transformations belong to that audit.
function(native_draw_packet_free input output)
    set(body "${input}")
    foreach(helper sub_8213DF00 sub_8213DB60 sub_8213DDA0 sub_8213DC20)
        string(REPLACE "${helper}(ctx, base);" "// Audited Xbox packet encoding omitted." body "${body}")
    endforeach()
    # D938 returns a mask consumed by the caller. Its mode is zero at these sites.
    string(REPLACE "sub_8213D938(ctx, base);" "ctx.r3.u64 = ctx.r4.u64 & ~uint64_t(0x100);" body "${body}")
    string(REPLACE "sub_8213ECB0(ctx, base);" "__imp__edf_native_main_state_cpu_tail(ctx, base);" body "${body}")
    set(${output} "${body}" PARENT_SCOPE)
endfunction()
