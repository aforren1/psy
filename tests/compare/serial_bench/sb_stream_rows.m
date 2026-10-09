function R = sb_stream_rows(R, api, mode, S, cfg)
%SB_STREAM_ROWS  Metrics of one stream run, and its raw CSV.
%   S.reads   cell of uint8 rows, in arrival order
%   S.host    host time after each read returned (us; NaN when the bytes were
%             collected after the fact, as from IOPort's background reader)
%   S.stamp   the API's own stamp for each read (us; NaN if none)
%   S.probe   struct t0, t2, rx, tx, ok: echo exchanges before and after
%   S.nlines  lines the board was asked to send
%   S.cpu_s, S.wall_s  process CPU time and wall time of the receive phase
%   S.note    how this API and mode receive
%   Each A line gets the stamps of the read that brought its newline.
    part = 'stream';
    tag = mode;
    nr = numel(S.reads);
    len = cellfun(@numel, S.reads(:));
    buf = [S.reads{:}];
    if isempty(buf), buf = zeros(1, 0, 'uint8'); end
    ridx = repelem((1:nr)', len);
    nl = find(buf == 10);
    if isempty(nl)
        starts = zeros(1, 0);
        valid = false(1, 0);
    else
        starts = [1, nl(1:end-1) + 1];
        lens = nl - starts + 1;
        valid = lens == 26 & buf(starts) == uint8('A');
    end
    sv = starts(valid);
    M = double(buf(sv(:) + (0:25))) - 48;
    if isempty(sv), M = zeros(0, 26); end
    w = 10 .^ (9:-1:0)';
    tdev = M(:, 3:12) * w;
    k = M(:, 16:25) * w;
    line_read = ridx(nl(valid));
    host = S.host(:); stamp = S.stamp(:);
    lh = host(line_read);
    ls = stamp(line_read);

    nu = numel(unique(k));
    R = sb_add(R, api, part, [tag '_lines_missed'], S.nlines - nu, sprintf('of %d lines (count); %s', S.nlines, S.note));
    R = sb_add(R, api, part, [tag '_lines_duplicated'], numel(k) - nu, 'count');
    R = sb_add(R, api, part, [tag '_bad_lines'], sum(~valid), 'lines of the wrong length or kind (count)');
    if nr > 0 && ~isempty(line_read)
        per = accumarray(line_read, 1, [nr, 1]);
        per = per(per > 0);
        R = sb_add(R, api, part, [tag '_lines_per_read'], per, 'lines completed by one read (or callback)');
        R = sb_add(R, api, part, [tag '_coalesced_pct'], 100 * sum(per(per > 1)) / sum(per), ...
                   'percent of lines that arrived in a read with another line');
    end
    R = sb_add(R, api, part, [tag '_cpu_pct'], 100 * S.cpu_s / S.wall_s, ...
               sprintf('MATLAB process CPU time / wall time over %.1f s, percent of one core', S.wall_s));

    P = S.probe;
    pok = P.ok(:);
    if sum(pok) >= 2 && ~isempty(tdev)
        ref = P.rx(find(pok, 1));
        F = sb_fit(P.t0(pok), P.t2(pok), sb_unwrap(P.rx(pok), ref), sb_unwrap(P.tx(pok), ref));
        if F.ok
            tmap = sb_map(F, sb_unwrap(tdev, ref));
            if any(~isnan(lh))
                R = sb_add(R, api, part, [tag '_lat_host'], lh - tmap, ...
                           sprintf('host time after the read returned minus mapped board send stamp (+-%.0f us)', F.unc_us));
            end
            if any(~isnan(ls))
                R = sb_add(R, api, part, [tag '_lat_stamp'], ls - tmap, ...
                           sprintf('API receive stamp minus mapped board send stamp (+-%.0f us)', F.unc_us));
            end
            R = sb_add(R, api, part, [tag '_fit_unc'], F.unc_us, 'half the best probe path time');
            R = sb_add(R, api, part, [tag '_fit_ppm'], F.ppm, sprintf('%d points over %.1f s', F.npts, F.span_s));
        end
    end
    sb_csv(fullfile(cfg.out, sprintf('%s_stream_%s.csv', api, tag)), ...
           {'k', 'board_t_us', 'read_index', 'host_us', 'stamp_us'}, ...
           [k, tdev, line_read, lh, ls]);
end
