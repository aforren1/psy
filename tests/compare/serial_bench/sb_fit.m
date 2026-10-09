function F = sb_fit(t0, t2, rx, tx)
%SB_FIT  Map the echo board's clock to the host clock from echo brackets.
%   t0: host time before the command was written; t2: host time after the
%   answer was read; rx, tx: the board's stamps of the command and of the
%   answer (unwrapped, us). All inputs are column vectors of good exchanges.
%
%   Method. The path time of an exchange is (t2 - t0) - (tx - rx): the round
%   trip without the board's own turnaround. In each 200 ms bucket of host
%   time the exchange with the smallest path time is kept, as ysp/rt.h's
%   BRACKET fit and PsychDataPixx's sync filter do. Each kept exchange gives
%   the pair (board time (rx + tx) / 2, host time (t0 + t2) / 2), which
%   assumes that the two one-way delays of that best exchange are equal.
%   A line through the pairs (least squares; slope fixed at 1 when the pairs
%   span less than 2 s) is the map.
%
%   What it is worth. The map is exact up to half the asymmetry of the best
%   exchanges, which is at most half their path time: F.unc_us. A one-way
%   delay computed with the map is therefore known to +-F.unc_us; its
%   variation from exchange to exchange is measured, not assumed. F.spread_us
%   is the p99 distance of the kept pairs from the line. F.ppm is the board
%   clock's rate error against the host clock, positive when it runs fast.
    F = struct('ok', false, 'x0', NaN, 'y0', NaN, 'slope', 1, 'ppm', NaN, ...
               'unc_us', NaN, 'spread_us', NaN, 'npts', 0, 'span_s', 0);
    if numel(t0) < 2, return; end
    path = (t2 - t0) - (tx - rx);
    b = floor((t0 - t0(1)) / 200e3);
    ub = unique(b);
    keep = zeros(numel(ub), 1);
    for i = 1:numel(ub)
        j = find(b == ub(i));
        [~, m] = min(path(j));
        keep(i) = j(m);
    end
    x = (rx(keep) + tx(keep)) / 2;
    y = (t0(keep) + t2(keep)) / 2;
    F.x0 = x(end);
    F.npts = numel(keep);
    F.span_s = (y(end) - y(1)) / 1e6;
    if F.span_s >= 2 && F.npts >= 3
        p = polyfit(x - F.x0, y, 1);
        F.slope = p(1);
        F.y0 = p(2);
    else
        F.y0 = median(y - (x - F.x0));
    end
    F.ppm = (1 / F.slope - 1) * 1e6;     % positive: the board's clock runs fast
    F.unc_us = min(path(keep)) / 2;
    s = sb_stats(abs(y - sb_map(F, x)));
    F.spread_us = s.p99;
    F.ok = true;
end
