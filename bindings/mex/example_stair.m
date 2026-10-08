% example_stair.m - ysp_stair MEX demo (MATLAB / Octave)
%
% A 1-up-3-down staircase in log contrast against a simulated 2AFC observer.
% Build first (run build.m), then run this script from this directory.

rng(1);
alpha = -1.5; beta = 3.5;                 % observer: threshold and slope, log10 units
pc = @(x) 0.5 + 0.48 * (1 - exp(-10 .^ (beta * (log10(x) - alpha))));

h = ysp_stair('open', struct( ...
    'start', 0.5, 'n_down', 3, 'step_type', 'log', ...
    'steps', [0.3 0.15 0.075], 'min', 0.001, 'max', 1, 'stop_reversals', 12));

while ~ysp_stair('done', h)
    x = ysp_stair('next', h);             % what the rule proposes
    r = ysp_stair('simulate_response', pc(x), rand());
    ysp_stair('update', h, x, r);         % what was shown, and the response
end

thr = ysp_stair('estimate', h, 'reversals');
fprintf('ysp_stair %s: %d trials, %d reversals, stop: %s\n', ysp_stair('version'), ...
        ysp_stair('n_trials', h), ysp_stair('n_reversals', h), ysp_stair('stop_reason', h));
fprintf('estimate %.4f (log10 %.3f) from %d reversals; converges on p = %.3f\n', thr, log10(thr), ...
        ysp_stair('estimate_count', h, 'reversals'), ysp_stair('convergence_p', 1, 3));
hist = ysp_stair('history', h);           % a struct of columns: struct2table(hist)
fprintf('first five proposals: %s\n', mat2str(hist.proposed(1:5).', 4));
ysp_stair('close', h);
