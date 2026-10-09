function [ok, rx, tx] = sb_reply(r, seq, device)
%SB_REPLY  Check the answer r (uint8) to the echo command seq.
%   Loopback: r must equal the command. Echo board: r must be the line
%   'R <seq> <t_rx> <t_tx>\n'; rx and tx are the board's micros() stamps.
    rx = NaN;
    tx = NaN;
    cmd = sb_cmd(seq, device);
    if ~strcmp(device, 'echo')
        ok = isequal(uint8(r(:)'), cmd);
        return;
    end
    ok = false;
    if numel(r) ~= 35 || r(1) ~= uint8('R') || r(35) ~= 10
        return;
    end
    d = double(r(:)') - 48;
    w = 10 .^ (9:-1:0)';
    if d(3:12) * w ~= seq
        return;
    end
    rx = d(14:23) * w;
    tx = d(25:34) * w;
    ok = true;
end
