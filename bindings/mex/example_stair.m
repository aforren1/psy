% example_stair.m - psy_stair MEX demo (MATLAB / Octave)
%
% A 1-up-3-down staircase in log contrast against a simulated 2AFC observer.
% Build first (run build.m), then run this script from this directory.

rng(1);
alpha = -1.5; beta = 3.5;                 % observer: threshold and slope, log10 units
pc = @(x) 0.5 + 0.48 * (1 - exp(-10 .^ (beta * (log10(x) - alpha))));

h = psy_stair('open', struct( ...
    'start', 0.5, 'n_down', 3, 'step_type', 'log', ...
    'steps', [0.3 0.15 0.075], 'min', 0.001, 'max', 1, 'stop_reversals', 12));

while ~psy_stair('done', h)
    x = psy_stair('next', h);             % what the rule proposes
    r = psy_stair('simulate_response', pc(x), rand());
    psy_stair('update', h, x, r);         % what was shown, and the response
end

thr = psy_stair('estimate', h, 'reversals');
fprintf('psy_stair %s: %d trials, %d reversals, stop: %s\n', psy_stair('version'), ...
        psy_stair('n_trials', h), psy_stair('n_reversals', h), psy_stair('stop_reason', h));
fprintf('estimate %.4f (log10 %.3f) from %d reversals; converges on p = %.3f\n', thr, log10(thr), ...
        psy_stair('estimate_count', h, 'reversals'), psy_stair('convergence_p', 1, 3));
hist = psy_stair('history', h);           % a struct of columns: struct2table(hist)
fprintf('first five proposals: %s\n', mat2str(hist.proposed(1:5).', 4));
psy_stair('close', h);
