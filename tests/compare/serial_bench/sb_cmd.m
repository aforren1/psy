function [cmd, L] = sb_cmd(seq, device)
%SB_CMD  The echo command for sequence number seq and the length of its answer.
%   'e <seq>\n' with seq zero-padded to 10 digits: 13 bytes. A loopback
%   returns the same 13 bytes; firmware/ysp_echo answers with a 35-byte R line.
    cmd = uint8(sprintf('e %010d\n', seq));
    if strcmp(device, 'echo')
        L = 35;
    else
        L = 13;
    end
end
