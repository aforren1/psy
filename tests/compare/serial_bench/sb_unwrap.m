function u = sb_unwrap(v, ref)
%SB_UNWRAP  Unwrap the board's 32-bit micros() against ref, the first stamp
%   of the session: valid while the session lasts less than 71.6 minutes.
    u = ref + mod(v - ref, 2^32);
end
