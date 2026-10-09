function tf = sb_want(cfg, part)
%SB_WANT  True if serial_bench was asked to run this part.
    tf = any(strcmp(cfg.parts, part));
end
