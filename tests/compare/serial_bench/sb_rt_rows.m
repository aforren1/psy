function R = sb_rt_rows(R, api, tag, X, cfg)
%SB_RT_ROWS  Metrics of one round-trip run, and its raw CSV.
%   X.t0  host time before the write call      X.t1  after the write call
%   X.t2  after the read call that completed the answer
%   X.when  the API's own receive stamp (IOPort), else NaN
%   X.rx, X.tx  the board's stamps (echo board only), X.ok  answer valid
    part = 'rt';
    ok = X.ok(:);
    nbad = sum(~ok);
    R = sb_add(R, api, part, [tag '_failed'], nbad, 'exchanges with a missing or wrong answer (count)');
    t0 = X.t0(ok); t1 = X.t1(ok); t2 = X.t2(ok);
    R = sb_add(R, api, part, [tag '_rtt'], t2 - t0, 'write call start to read call return');
    R = sb_add(R, api, part, [tag '_write_call'], t1 - t0, 'write call duration inside the exchange');
    R = sb_add(R, api, part, [tag '_write_ret_to_read_ret'], t2 - t1, 'write call return to read call return');
    w = X.when(ok);
    if any(~isnan(w))
        R = sb_add(R, api, part, [tag '_stamp_minus_return'], w - t2, ...
                   'API receive stamp minus host time after the read returned');
    end
    if strcmp(cfg.device, 'echo') && sum(ok) >= 2
        rx = X.rx(ok); tx = X.tx(ok);
        ref = rx(1);
        rx = sb_unwrap(rx, ref);
        tx = sb_unwrap(tx, ref);
        F = sb_fit(t0, t2, rx, tx);
        if F.ok
            R = sb_add(R, api, part, [tag '_host_to_board'], sb_map(F, rx) - t0, ...
                       sprintf('write call start to board read (+-%.0f us from the fit)', F.unc_us));
            R = sb_add(R, api, part, [tag '_write_ret_to_board'], sb_map(F, rx) - t1, ...
                       sprintf('write call return to board read (+-%.0f us)', F.unc_us));
            R = sb_add(R, api, part, [tag '_board_to_host'], t2 - sb_map(F, tx), ...
                       sprintf('board send stamp to read call return (+-%.0f us)', F.unc_us));
            R = sb_add(R, api, part, [tag '_board_turnaround'], tx - rx, 'board: command read to answer stamp');
            if any(~isnan(w))
                R = sb_add(R, api, part, [tag '_stamp_minus_board_send'], w - sb_map(F, tx), ...
                           sprintf('API receive stamp minus mapped board send stamp (+-%.0f us)', F.unc_us));
            end
            R = sb_add(R, api, part, [tag '_fit_unc'], F.unc_us, 'half the best path time: bound of the one-way split');
            R = sb_add(R, api, part, [tag '_fit_spread'], F.spread_us, 'p99 distance of the fit points from the line');
            R = sb_add(R, api, part, [tag '_fit_ppm'], F.ppm, sprintf('board clock rate error, %d points over %.1f s', F.npts, F.span_s));
        end
    end
    sb_csv(fullfile(cfg.out, sprintf('%s_rt_%s.csv', api, tag)), ...
           {'seq', 't0_us', 't1_us', 't2_us', 'when_us', 'board_rx_us', 'board_tx_us', 'ok'}, ...
           [X.seq(:), X.t0(:), X.t1(:), X.t2(:), X.when(:), X.rx(:), X.tx(:), double(X.ok(:))]);
end
