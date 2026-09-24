% example_gp.m - psy_gp MEX demo (MATLAB / Octave)
%
% A contrast threshold that varies with spatial frequency, found with a GP
% classifier and the level-set straddle (LSE), against a simulated yes/no
% observer. Build first (run build.m).

rng(3);
thr_at = @(sf) -2 + 0.8 * (sf - 0.6) .^ 2;          % log10 contrast threshold at log10 SF
pyes = @(x) 0.02 + 0.96 * 0.5 * erfc(-((x(2) - thr_at(x(1))) / 0.15) / sqrt(2));

d = struct('lo', [0 -3], 'hi', [1.5 0], 'intensity_dim', 2, ...
           'acq', 'lse', 'target_p', 0.5, 'grid', [11 21], 'refine_steps', 2, ...
           'n_init', 10, 'fit', true, 'fit_every', 10, 'stop_trials', 80);
h = psy_gp('open', d);
tic;
while ~psy_gp('done', h)
    x = psy_gp('next', h);                           % 1 x 2: [log SF, log contrast]
    psy_gp('update', h, x, double(rand() < pyes(x)));
end
fprintf('psy_gp %s: %d trials in %.2f s\n', psy_gp('version'), psy_gp('n_trials', h), toc);
for sf = [0.3 0.6 1.2]
    [t, lo, hi] = psy_gp('threshold', h, sf);
    fprintf('log SF %.1f: threshold %.3f [%.3f, %.3f], truth %.3f\n', sf, t, lo, hi, thr_at(sf));
end
hy = psy_gp('get_hyper', h);
fprintf('lengthscales %s, log marginal %.2f\n', mat2str(hy.lengthscale, 3), psy_gp('log_marginal', h));

% The field on a grid, in one blocked pass.
[sfg, cg] = meshgrid(linspace(0, 1.5, 16), linspace(-3, 0, 31));
p = reshape(psy_gp('predict_p_many', h, [sfg(:) cg(:)]), size(sfg));
fprintf('field: p ranges %.2f to %.2f over %d points\n', min(p(:)), max(p(:)), numel(p));
psy_gp('close', h);
