function s = sb_stats(v)
%SB_STATS  n, median, p99, max, min and mean of v, NaNs dropped.
%   p99 is the order statistic at ceil(0.99 n), so for n < 100 it is the max.
    v = sort(v(~isnan(v)));
    n = numel(v);
    s = struct('n', n, 'median', NaN, 'p99', NaN, 'max', NaN, 'min', NaN, 'mean', NaN);
    if n == 0, return; end
    s.median = median(v);
    s.p99 = v(max(1, ceil(0.99 * n)));
    s.max = v(end);
    s.min = v(1);
    s.mean = mean(v);
end
