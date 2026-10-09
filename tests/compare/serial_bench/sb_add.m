function R = sb_add(R, api, part, metric, values, note)
%SB_ADD  Append one metric (a vector of samples in microseconds, or a count)
%   to the result list R. serial_bench writes R to the CSV files at the end.
    if nargin < 6, note = ''; end
    k = numel(R) + 1;
    R(k).api = api;
    R(k).part = part;
    R(k).metric = metric;
    R(k).values = double(values(:));
    R(k).note = note;
end
