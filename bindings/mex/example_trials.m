% example_trials.m - ysp_trials MEX demo (MATLAB / Octave)
%
% Three interleaved staircases (1-up-2-down, 1-up-3-down, 1-up-4-down) and a
% catch condition on about one trial in ten, never two catch trials in a row.
% The level each staircase trial showed is kept as the trial's 8-byte record.
% Build first (run build.m).

rng(4);
pc = @(x) 0.5 + 0.48 * (1 - exp(-10 .^ (3.5 * (log10(x) + 1.5))));

stairs = cell(1, 3);
for k = 1:3
    stairs{k} = ysp_stair('open', struct('start', 0.3, 'n_down', k + 1, 'step_type', 'log', ...
        'steps', [0.2 0.1], 'min', 0.001, 'max', 1, 'stop_reversals', 8));
end

d = struct('n_conditions', 1, 'reps', 12, 'order', 'constrained', ...
           'track_rate', 0.9, 'rng', uint64(20260923), 'record_size', 8);
d.constraints = ysp_trials('min_gap', 'condition', 1, 1);
d.tracks = {{'stair', stairs{1}}, {'stair', stairs{2}}, {'stair', stairs{3}}};
h = ysp_trials('open', d);

fprintf('%s', ysp_trials('format_meta', h));
ti = ysp_trials('next', h);
while ~isempty(ti)
    if ti.track > 0                                   % a staircase trial
        s = stairs{ti.track};
        x = ysp_stair('next', s);
        r = double(rand() < pc(x));
        ysp_stair('update', s, x, r);
        ysp_trials('update', h, r, x);                % the level as the record
    else                                              % a catch trial
        ysp_trials('update', h, double(rand() < 0.05));
    end
    ti = ysp_trials('next', h);
end

H = ysp_trials('history', h);
fprintf('%d trials: %d catch, %d staircase\n', numel(H.outcome), sum(H.condition == 1), sum(H.track > 0));
fprintf('%s', ysp_trials('format_header', h));
for i = 1:3
    fprintf('%s', ysp_trials('format_row', h, i));
end
for k = 1:3
    fprintf('staircase %d: estimate %.4f after %d trials\n', k, ...
            ysp_stair('estimate', stairs{k}), ysp_stair('n_trials', stairs{k}));
    ysp_stair('close', stairs{k});
end
fprintf('catch trials answered yes: %.2f\n', ysp_trials('proportion', h, 1));
ysp_trials('close', h);
