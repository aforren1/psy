function [n, pace_us] = sb_pace(cfg, nbytes)
%SB_PACE  Samples and the gap between write calls for a write of nbytes.
%   The gap lets the previous write leave the port first, so each call is
%   timed against an empty transmit queue: 1.5 times the wire time at
%   cfg.baud (10 bits per byte), at least 1 ms. A native-USB board ignores
%   the baud rate, so with cfg.device = 'echo' the gap is 1 ms. The sample
%   count is cut so that one size takes at most cfg.max_block_s.
    if strcmp(cfg.device, 'echo')
        pace_us = 1000;
    else
        pace_us = max(1000, 1.5 * nbytes * 10 / cfg.baud * 1e6);
    end
    n = min(cfg.n_calls, max(50, floor(cfg.max_block_s * 1e6 / pace_us)));
end
