function test_mex()
%TEST_MEX  Key checks of the psy_stair, psy_quest, psy_gp and psy_trials MEX
%   functions. Runs in MATLAB and in Octave. Prints PASS, or stops with the
%   error of the first check that failed.
%
%   Build first (run build.m), then from this directory:  test_mex
%
%   Every random draw comes from psy_trials('splitmix'), so both interpreters
%   see the same stream and the same numbers.

    here = fileparts(mfilename('fullpath'));
    addpath(here);
    global PSY_TEST_STATE %#ok<GVMIS>
    PSY_TEST_STATE = uint64(20260923);

    fprintf('psy_stair %s, psy_quest %s, psy_gp %s, psy_trials %s\n', ...
        psy_stair('version'), psy_quest('version'), psy_gp('version'), psy_trials('version'));

    test_stair();
    test_quest();
    test_gp();
    test_trials();
    fprintf('PASS\n');
end

% ---------------------------------------------------------------------------

function u = urand()
    global PSY_TEST_STATE %#ok<GVMIS>
    [u, PSY_TEST_STATE] = psy_trials('splitmix', PSY_TEST_STATE);
end

function check(cond, msg, varargin)
    if ~cond
        error('test_mex:fail', ['FAIL: ' msg], varargin{:});
    end
end

function expect_error(id, f)
    try
        f();
    catch e
        check(strcmp(e.identifier, id), 'expected error %s, got %s (%s)', id, e.identifier, e.message);
        return;
    end
    error('test_mex:fail', 'FAIL: expected error %s, got none', id);
end

function out = failing_model(varargin) %#ok<STOUT>
    error('user:model', 'model failed');
end

function p = weibull_log(x, a, b, g, l)
    p = g + (1 - g - l) .* (1 - exp(-10 .^ (b .* (x - a))));
end

% ---------------------------------------------------------------------------

function test_stair()
    % A hand-derived 1-up-2-down track: correct, correct steps down; an
    % incorrect steps up and reverses.
    d = struct('start', 10, 'n_down', 2, 'steps', 1, 'stop_trials', 6);
    h = psy_stair('open', d);
    resp = [1 1 0 1 1 1];
    want = [10 10 9 10 10 9];
    for t = 1:6
        x = psy_stair('next', h);
        check(x == want(t), 'stair proposal %d: %g, want %g', t, x, want(t));
        psy_stair('update', h, x, resp(t));
    end
    check(psy_stair('done', h), 'stair not done after stop_trials');
    check(strcmp(psy_stair('stop_reason', h), 'trials'), 'stair stop reason');
    check(psy_stair('n_reversals', h) == 2, 'stair reversal count %d', psy_stair('n_reversals', h));
    hist = psy_stair('history', h);
    check(isequal(hist.proposed.', want) && isequal(hist.response.', resp), 'stair history');
    check(isequal(psy_stair('reversal_trial', h).', [3 5]), 'stair reversal trials');
    check(abs(psy_stair('estimate', h, 'reversals') - 9.5) < 1e-12, 'stair estimate');
    check(abs(psy_stair('convergence_p', 1, 2) - sqrt(0.5)) < 1e-12, 'convergence_p');
    check(abs(psy_stair('weighted_scale', 0.75) - 1/3) < 1e-12, 'weighted_scale');
    expect_error('psy_stair:arg', @() psy_stair('update', h, 5, 2));
    psy_stair('close', h);
    expect_error('psy_stair:handle', @() psy_stair('next', h));
    expect_error('psy_stair:open', @() psy_stair('open', struct('start', 1, 'steps', 1)));
    expect_error('psy_stair:arg', @() psy_stair('open', struct('start', 1, 'steps', 1, 'stop_trial', 3)));
    expect_error('psy_stair:usage', @() psy_stair('nosuchcommand'));

    % A 1-up-3-down log staircase against a simulated observer converges.
    d = struct('start', 0.5, 'n_down', 3, 'step_type', 'log', 'steps', [0.3 0.15 0.075], ...
               'min', 0.001, 'max', 1, 'stop_reversals', 14);
    h = psy_stair('open', d);
    while ~psy_stair('done', h)
        x = psy_stair('next', h);
        psy_stair('update', h, x, psy_stair('simulate_response', weibull_log(log10(x), -1.5, 3.5, 0.5, 0.02), urand()));
    end
    thr = log10(psy_stair('estimate', h));
    check(abs(thr + 1.45) < 0.3, 'stair 1-up-3-down estimate %.3f far from the 79%% point', thr);
    psy_stair('close', h);
    fprintf('psy_stair: ok\n');
end

% ---------------------------------------------------------------------------

function d = psi_desc()
    d = struct();
    d.stim = {{-3, 0, 31}};
    d.param = {{-3, 0, 61}, {0.5, 6, 12}, 0.5, 0.02};
    d.stop_trials = 100;
end

function test_quest()
    truth = [-1.5 3.5 0.5 0.02];

    % A Psi run recovers its threshold.
    h = psy_quest('open', psi_desc());
    while ~psy_quest('done', h)
        [i, x] = psy_quest('next', h);
        k = psy_quest('simulate', h, i, truth, urand());
        check(abs(x - psy_quest('stim_value', h, i)) < 1e-15, 'next second output');
        psy_quest('update', h, i, k);
    end
    est = psy_quest('estimate', h, 'mean');
    check(abs(est(1) - truth(1)) < 0.2, 'Psi threshold %.3f, truth %.3f', est(1), truth(1));
    post = psy_quest('posterior', h);
    check(isequal(size(post), [61 12]), 'posterior shape');
    check(abs(sum(post(:)) - 1) < 1e-9, 'posterior mass');
    m = psy_quest('marginal', h, 1);
    check(max(abs(m - sum(post, 2))) < 1e-12, 'marginal against the posterior');
    hs = psy_quest('history', h);
    check(numel(hs.outcome) == 100 && all(hs.stim_index >= 1), 'quest history');
    psy_quest('close', h);

    % The same model through pf_batch fills the same table.
    d = psi_desc();
    d.stop_trials = 20;
    d2 = d;
    d2.pf_batch = @(s, P) [1 - weibull_log(s(1), P(:,1), P(:,2), P(:,3), P(:,4)), ...
                               weibull_log(s(1), P(:,1), P(:,2), P(:,3), P(:,4))];
    h1 = psy_quest('open', d);
    h2 = psy_quest('open', d2);
    for t = 1:20
        i1 = psy_quest('next', h1);
        i2 = psy_quest('next', h2);
        check(i1 == i2, 'pf_batch selection differs at trial %d', t);
        k = psy_quest('simulate', h1, i1, truth, urand());
        psy_quest('update', h1, i1, k);
        psy_quest('update', h2, i2, k);
    end
    dp = max(abs(psy_quest('posterior', h1) - psy_quest('posterior', h2)));
    check(max(dp(:)) < 1e-6, 'pf_batch posterior differs by %g', max(dp(:)));
    psy_quest('close', h2);

    % Save between an update and the next selection, resume, and continue:
    % the two sessions agree bit for bit.
    b = psy_quest('save', h1);
    check(isa(b, 'uint8'), 'save returns uint8');
    h3 = psy_quest('load', b, d);
    for t = 1:10
        i1 = psy_quest('next', h1);
        i3 = psy_quest('next', h3);
        check(i1 == i3, 'resumed selection differs at trial %d', t);
        k = psy_quest('simulate', h1, i1, truth, urand());
        psy_quest('update', h1, i1, k);
        psy_quest('update', h3, i3, k);
    end
    check(isequal(psy_quest('posterior', h1), psy_quest('posterior', h3)), 'resumed posterior differs');
    psy_quest('close', h3);

    % Async: the same responses give the same posterior as the plain loop.
    ha = psy_quest('open', d);
    hs = psy_quest('open', d);
    psy_quest('async_start', ha, struct('queue_depth', 1));
    snap = psy_quest('async_poll', ha);
    check(snap.seq == 0, 'first snapshot seq');
    for t = 1:8
        is = psy_quest('next', hs);
        check(snap.proposed == is, 'async proposal differs at trial %d', t);
        k = psy_quest('simulate', hs, is, truth, urand());
        psy_quest('update', hs, is, k);
        seq = psy_quest('async_submit', ha, snap.proposed, k);
        snap = psy_quest('async_wait', ha, seq, 5);
        check(snap.seq >= seq && snap.n_trials == t, 'async wait');
    end
    expect_error('psy_quest:busy', @() psy_quest('next', ha));
    psy_quest('async_stop', ha);
    check(isequal(psy_quest('posterior', ha), psy_quest('posterior', hs)), 'async posterior differs');
    psy_quest('close', hs);

    % A MATLAB rng refuses the async layer; an integer seed does not.
    d3 = d; d3.rng = @() rand(); d3.subset_size = 8;
    hr = psy_quest('open', d3);
    expect_error('psy_quest:arg', @() psy_quest('async_start', hr));
    psy_quest('close', hr);
    d3.rng = 7;
    hr = psy_quest('open', d3);
    psy_quest('async_start', hr);
    psy_quest('async_stop', hr);
    psy_quest('close', hr);

    % Error ids.
    expect_error('psy_quest:arg', @() psy_quest('update', ha, 0, 1));
    expect_error('psy_quest:arg', @() psy_quest('update', ha, 1, 5));
    d4 = d; d4.pf_batch = @failing_model;
    expect_error('user:model', @() psy_quest('open', d4));
    d4.pf_batch = @(s, P) ones(3, 3);
    expect_error('psy_quest:callback', @() psy_quest('open', d4));
    expect_error('psy_quest:open', @() psy_quest('open', rmfield(d, 'stop_trials')));
    expect_error('psy_quest:load', @() psy_quest('load', uint8([1 2 3]), d));
    psy_quest('close', ha);
    psy_quest('close', h1);
    expect_error('psy_quest:handle', @() psy_quest('next', h1));
    fprintf('psy_quest: ok\n');
end

% ---------------------------------------------------------------------------

function test_gp()
    % 1-D Bernoulli detection, threshold at -1.5 log units for p = 0.75
    % with guess 0.5: truth p(x) = 0.5 + 0.5 * Phi((x + 1.5) / 0.3).
    ptrue = @(x) 0.5 + 0.5 * 0.5 * erfc(-((x + 1.5) / 0.3) / sqrt(2));
    d = struct('lo', -3, 'hi', 0, 'guess', 0.5, 'target_p', 0.75, 'n_init', 6, ...
               'n_candidates', 61, 'fit', true, 'fit_every', 10, 'stop_trials', 60);
    h = psy_gp('open', d);
    for t = 1:40
        [x, idx] = psy_gp('next', h); %#ok<ASGLU>
        psy_gp('update', h, x, double(urand() < ptrue(x)));
    end
    b = psy_gp('save', h);
    h2 = psy_gp('load', b, d);
    while ~psy_gp('done', h)
        x = psy_gp('next', h);
        x2 = psy_gp('next', h2);
        check(isequal(x, x2), 'resumed GP proposal differs');
        y = double(urand() < ptrue(x));
        psy_gp('update', h, x, y);
        psy_gp('update', h2, x2, y);
    end
    [thr, lo, hi] = psy_gp('threshold', h);
    check(abs(thr + 1.5) < 0.35, 'GP threshold %.3f, truth -1.5', thr);
    check(lo <= thr && thr <= hi, 'GP band does not contain the threshold');
    [thr2] = psy_gp('threshold', h2);
    check(thr == thr2, 'resumed GP threshold differs');
    p = psy_gp('predict_p_many', h, [-3; -1.5; 0]);
    check(numel(p) == 3 && p(1) < p(3), 'predict_p_many');
    hy = psy_gp('get_hyper', h);
    check(isfield(hy, 'lengthscale') && hy.lengthscale > 0, 'get_hyper');
    hist = psy_gp('history', h);
    check(size(hist.x, 1) == 60 && islogical(hist.init), 'GP history');
    expect_error('psy_gp:arg', @() psy_gp('update', h, [0 0], 1));
    expect_error('psy_gp:arg', @() psy_gp('open', struct('lo', 0, 'hi', 1, 'stop_trials', 5, 'lik', 'nosuch')));
    psy_gp('close', h);
    psy_gp('close', h2);

    % Async with fit_in_idle off equals the plain loop.
    d.fit = false; d.stop_trials = 12;
    ha = psy_gp('open', d);
    hs = psy_gp('open', d);
    psy_gp('async_start', ha);
    snap = psy_gp('async_poll', ha);
    for t = 1:10
        xs = psy_gp('next', hs);
        check(isequal(xs, snap.x), 'GP async proposal differs at trial %d', t);
        y = double(urand() < ptrue(xs));
        psy_gp('update', hs, xs, y);
        seq = psy_gp('async_submit', ha, snap.x, y);
        snap = psy_gp('async_wait', ha, seq, 10);
    end
    psy_gp('async_stop', ha);
    check(psy_gp('log_marginal', ha) == psy_gp('log_marginal', hs), 'GP async posterior differs');
    psy_gp('close', ha);
    psy_gp('close', hs);
    fprintf('psy_gp: ok\n');
end

% ---------------------------------------------------------------------------

function test_trials()
    % Method of constant stimuli: 2 x 3 factorial, constrained order.
    d = struct('reps', 6, 'order', 'constrained', 'rng', uint64(42));
    d.factors = {{'ori', 2}, {'con', 3}};
    d.constraints = psy_trials('max_run', 'ori', 'any', 2);
    h = psy_trials('open', d);
    check(psy_trials('n_conditions', h) == 6, 'factorial size');
    check(psy_trials('condition_from_levels', h, [2 3]) == 6, 'condition_from_levels');
    check(isequal(psy_trials('levels', h, 4), [2 1]), 'levels');
    for t = 1:15
        ti = psy_trials('next', h);
        psy_trials('update', h, double(urand() < 0.7));
    end
    % Save mid-session, resume, and finish both.
    b = psy_trials('save', h);
    d2 = d; d2.rng = psy_trials('rng_state', h);
    h2 = psy_trials('load', b, d2);
    while true
        ti = psy_trials('next', h);
        ti2 = psy_trials('next', h2);
        check(isequal(ti, ti2), 'resumed trial differs');
        if isempty(ti), break; end
        y = double(urand() < 0.7);
        psy_trials('update', h, y);
        psy_trials('update', h2, y);
    end
    H = psy_trials('history', h);
    check(isequal(H, psy_trials('history', h2)), 'resumed history differs');
    ori = zeros(numel(H.condition), 1);
    for t = 1:numel(ori), ori(t) = psy_trials('level', h, H.condition(t), 1); end
    run = 1;
    for t = 2:numel(ori)
        if ori(t) == ori(t-1), run = run + 1; else, run = 1; end
        check(run <= 2, 'max_run broken at trial %d', t);
    end
    check(numel(H.condition) == 36 && all(H.done), 'MOCS trial count');
    hdr = psy_trials('format_header', h);
    check(ischar(hdr) && hdr(end) == sprintf('\n'), 'format_header');
    expect_error('psy_trials:order', @() psy_trials('update', h, 1));
    psy_trials('close', h);
    psy_trials('close', h2);

    % Three staircases and a catch condition, never two catch trials in a
    % row; the level of each staircase trial is the 8-byte record.
    stairs = cell(1, 3);
    for k = 1:3
        stairs{k} = psy_stair('open', struct('start', 0.3, 'n_down', k + 1, 'step_type', 'log', ...
            'steps', [0.2 0.1], 'min', 0.001, 'max', 1, 'stop_reversals', 6));
    end
    d = struct('n_conditions', 1, 'reps', 10, 'order', 'constrained', ...
               'track_rate', 0.9, 'rng', 7, 'record_size', 8);
    d.constraints = psy_trials('min_gap', 'condition', 1, 1);
    d.tracks = {{'stair', stairs{1}}, {'stair', stairs{2}}, {'stair', stairs{3}}};
    h = psy_trials('open', d);
    levels = [];
    while true
        ti = psy_trials('next', h);
        if isempty(ti), break; end
        if ti.track > 0
            s = stairs{ti.track};
            x = psy_stair('next', s);
            r = psy_stair('simulate_response', weibull_log(log10(x), -1.5, 3.5, 0.5, 0.02), urand());
            psy_stair('update', s, x, r);
            psy_trials('update', h, r, x);
            levels(end+1) = x; %#ok<AGROW>
        else
            psy_trials('update', h, double(urand() < 0.5));
        end
    end
    check(psy_trials('done', h), 'trials not done');
    for k = 1:3, check(psy_stair('done', stairs{k}), 'staircase %d not done', k); end
    H = psy_trials('history', h);
    catchs = find(H.condition == 1);
    check(numel(catchs) == 10, 'catch trial count %d', numel(catchs));
    check(all(diff(catchs) > 1) || any(H.violation), 'two catch trials in a row');
    tr = find(H.track > 0);
    check(numel(tr) == numel(levels), 'track trial count');
    for j = 1:numel(tr)
        check(typecast(psy_trials('record', h, tr(j)), 'double') == levels(j), 'record %d', j);
    end
    check(all(psy_trials('record', h, catchs(1)) == 0), 'a catch trial record is zeroed');
    % A closed staircase handle surfaces as that module's error, from next.
    d.tracks = {{'stair', stairs{1}}};
    d.n_conditions = 1; d.track_rate = 1; d.constraints = [];
    psy_stair('close', stairs{1});
    h3 = psy_trials('open', d);
    expect_error('psy_stair:handle', @() psy_trials('next', h3));
    psy_trials('close', h3);
    for k = 2:3, psy_stair('close', stairs{k}); end
    expect_error('psy_trials:open', @() psy_trials('open', struct('n_conditions', 2)));
    expect_error('psy_trials:arg', @() psy_trials('open', struct('n_conditions', 2, 'reps', 1, 'nosuch', 1)));
    psy_trials('close', h);
    fprintf('psy_trials: ok\n');
end
