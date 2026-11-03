/* Embedded assets: the default config and the overlay font. */
    .section .rodata
    .balign 16
    .global ts_default_toml
    .hidden ts_default_toml
ts_default_toml:
    .incbin "default.toml"
    .byte 0
ts_default_toml_end:
    .balign 4
    .global ts_default_toml_size
    .hidden ts_default_toml_size
ts_default_toml_size:
    .int ts_default_toml_end - ts_default_toml - 1

    .balign 16
    .global ts_font_ttf
    .hidden ts_font_ttf
ts_font_ttf:
    .incbin "OpenSans-Regular.ttf"
ts_font_ttf_end:
    .balign 4
    .global ts_font_ttf_size
    .hidden ts_font_ttf_size
ts_font_ttf_size:
    .int ts_font_ttf_end - ts_font_ttf

    .section .note.GNU-stack,"",@progbits
