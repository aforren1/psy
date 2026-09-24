function compare_stair_palamedes(pal_dir)
%COMPARE_STAIR_PALAMEDES  psy_stair and psy_quest (MEX) against Palamedes.
%
%   compare_stair_palamedes('/path/to/Palamedes')
%   compare_stair_palamedes            % reads the PALAMEDES_DIR variable
%
%   Palamedes (Prins & Kingdom, palamedestoolbox.org) is free for
%   noncommercial use and may not be redistributed, so it is not vendored
%   here: download it and pass the folder that holds PAL_AMUD_setupUD.m.
%   Build the MEX first (run bindings/mex/build.m).
%
%   Part 1, PAL_AMUD against psy_stair. Fixed response sequences, one per
%   rule, fed to both; the proposals, the reversal trials and the length of
%   the run must agree. Palamedes steps 1-up-1-down until the first reversal
%   whatever the rule (PAL_AMUD_updateUD tests max(UD.reversal) < 1), which is
%   psy_stair's initial_rule, so every psy_stair desc here sets it.
%   Palamedes has no step schedule; a caller changes UD.stepSizeUp/Down
%   between trials. Two callers are run:
%     'after'    changes the step after the update that reversed. The
%                reversal trial itself then steps by the OLD size.
%     'predict'  works out before the update whether this response will
%                reverse, and sets the new size first, which is psy_stair's
%                documented order (the reversal trial takes the new size, as
%                in PsychoPy's StairHandler).
%   The 'after' rows are expected to differ from psy_stair, from the trial
%   after the first reversal on; they are printed, not failed.
%
%   Part 2, PAL_AMPM against psy_quest: Psi (threshold and slope, PAL_Gumbel)
%   and Psi-marginal (lapse marginalized). Palamedes drives: both select,
%   both are updated with Palamedes' stimulus and one outcome from a fixed
%   stream, and the posteriors are compared cell by cell. A selection that
%   differs is a TIE when Palamedes' own expected entropy at our stimulus is
%   within 1e-9 of its minimum. Palamedes' slope grid is log10(beta); the psy
%   axis gets 10.^ of it, the values PAL_AMPM_CreateLUT computes.
%
%   Prints a table per part and raises an error on any disagreement beyond
%   tolerance (1e-9 in level for the staircases, 1e-6 in posterior mass).

    if nargin < 1 || isempty(pal_dir)
        pal_dir = getenv('PALAMEDES_DIR');
    end
    if isempty(pal_dir) || ~exist(fullfile(pal_dir, 'PAL_AMUD_setupUD.m'), 'file')
        error('compare:setup', 'give the Palamedes folder that holds PAL_AMUD_setupUD.m (or set PALAMEDES_DIR)');
    end
    root = fileparts(fileparts(fileparts(mfilename('fullpath'))));
    addpath(fullfile(root, 'bindings', 'mex'));
    addpath(pal_dir);
    warning('off', 'PALAMEDES:AMPM_setupPM:priorTranspose');

    bad = compare_ud();
    bad = compare_pm() || bad;
    if bad
        error('compare:disagree', 'psy and Palamedes disagree beyond tolerance');
    end
    fprintf('agreement within tolerance on every case\n');
end

% ---------------------------------------------------------------------------

function r = responses(seed, n, p)
    % A fixed response sequence: n Bernoulli(p) draws from splitmix64.
    state = uint64(seed);
    r = zeros(1, n);
    for i = 1:n
        [u, state] = psy_trials('splitmix', state);
        r(i) = u < p;
    end
end

function bad = compare_ud()
    % name, up, down, step up, step down, start, xmin, xmax, stop criterion,
    % stop rule, step schedule (after reversal k use entry k+1), log, p(correct)
    C = {
        '1-up-2-down',        1, 2, 0.1,  0.1,  0,   [],   [],   'trials',    80, [],               false, 0.72
        '1-up-3-down',        1, 3, 0.1,  0.1,  0,   [],   [],   'trials',    80, [],               false, 0.80
        '2-up-1-down',        2, 1, 0.1,  0.1,  0,   [],   [],   'trials',    80, [],               false, 0.30
        '1-up-3-down, revs',  1, 3, 0.1,  0.1,  0,   [],   [],   'reversals', 12, [],               false, 0.80
        'weighted 1u1d',      1, 1, 0.3,  0.1,  0,   [],   [],   'trials',    80, [],               false, 0.75
        'limits',             1, 2, 0.25, 0.25, 0,   -0.4, 0.5,  'trials',    80, [],               false, 0.60
        'log10 steps',        1, 3, 0.2,  0.2,  -0.5, -3,  0,    'trials',    80, [],               true,  0.80
        'schedule, predict',  1, 2, 0.4,  0.4,  0,   [],   [],   'trials',    80, [0.4 0.2 0.1],    false, 0.72
        'schedule, after',    1, 2, 0.4,  0.4,  0,   [],   [],   'trials',    80, [0.4 0.2 0.1],    false, 0.72
        '2-up-2-down',        2, 2, 0.1,  0.1,  0,   [],   [],   'trials',    80, [],               false, 0.60
    };
    fprintf('\nPart 1: psy_stair %s against PAL_AMUD\n\n', psy_stair('version'));
    fprintf('%-20s %7s %7s %10s %12s  %s\n', 'case', 'trials', 'revs', 'max |dx|', 'first diff', 'verdict');
    bad = false;
    for c = 1:size(C, 1)
        [name, up, down, su, sd, x0, xmin, xmax, crit, rule, sched, islog, p] = C{c, :};
        resp = responses(1000 + c, 400, p);
        caller = '';
        if ~isempty(sched), caller = name(11:end); end

        % Palamedes.
        args = {'up', up, 'down', down, 'stepSizeUp', su, 'stepSizeDown', sd, ...
                'startValue', x0, 'stopCriterion', crit, 'stopRule', rule};
        if ~isempty(xmin), args = [args, {'xMin', xmin, 'xMax', xmax}]; end %#ok<AGROW>
        UD = PAL_AMUD_setupUD(args{:});
        t = 0;
        while ~UD.stop
            t = t + 1;
            if ~isempty(sched)
                nrev = sum(UD.reversal ~= 0);
                if strcmp(caller, 'predict') && will_reverse(UD, resp(t))
                    nrev = nrev + 1;
                end
                s = sched(min(nrev + 1, numel(sched)));
                UD = PAL_AMUD_setupUD(UD, 'stepSizeUp', s, 'stepSizeDown', s);
            end
            UD = PAL_AMUD_updateUD(UD, resp(t));
        end
        pal_x = UD.x(1:t);
        pal_rev = find(UD.reversal ~= 0);

        % psy_stair.
        d = struct('n_up', up, 'n_down', down, 'initial_rule', true);
        if islog
            d.start = 10 ^ x0; d.step_type = 'log';
            if ~isempty(xmin), d.min = 10 ^ xmin; d.max = 10 ^ xmax; end
        else
            d.start = x0;
            if ~isempty(xmin), d.min = xmin; d.max = xmax; d.use_limits = true; end
        end
        if isempty(sched)
            d.steps = su;
            if sd ~= su, d.step_down_scale = sd / su; end
        else
            d.steps = sched;
        end
        if strcmp(crit, 'trials'), d.stop_trials = rule; else, d.stop_reversals = rule; end
        h = psy_stair('open', d);
        k = 0;
        while ~psy_stair('done', h)
            k = k + 1;
            x = psy_stair('next', h);
            psy_stair('update', h, x, resp(k));
        end
        hist = psy_stair('history', h);
        psy_x = hist.proposed.';
        if islog, psy_x = log10(psy_x); end
        psy_rev = find(hist.reversal.');
        psy_stair('close', h);

        m = min(numel(pal_x), numel(psy_x));
        dx = abs(pal_x(1:m) - psy_x(1:m));
        first = find(dx > 1e-9, 1);
        rv = min(numel(pal_rev), numel(psy_rev));
        rfirst = find(pal_rev(1:rv) ~= psy_rev(1:rv), 1);
        same = isempty(first) && numel(pal_x) == numel(psy_x) && isempty(rfirst) && numel(pal_rev) == numel(psy_rev);
        if same
            verdict = 'identical';
            fd = '-';
        else
            if isempty(first), first = m + 1; end
            if ~isempty(rfirst), first = min(first, pal_rev(rfirst)); end
            fd = sprintf('%d', first);
            if strcmp(caller, 'after') || up > 1 && down > 1
                verdict = 'differs (expected, see below)';
            else
                verdict = 'DIFFERS';
                bad = true;
            end
        end
        fprintf('%-20s %3d/%-3d %3d/%-3d %10.2g %12s  %s\n', name, numel(pal_x), numel(psy_x), ...
                numel(pal_rev), numel(psy_rev), max([dx 0]), fd, verdict);
    end
    fprintf(['\n''schedule, after'': Palamedes applies a new step size from the trial after the\n' ...
             'reversal; psy_stair already steps the reversal trial by it. ''2-up-2-down'':\n' ...
             'PAL_AMUD keeps the run counter of the other direction when a response does not\n' ...
             'step (a correct response leaves UD.u alone), psy_stair resets both counters on\n' ...
             'every change of response direction, as PsychoPy does.\n']);
end

% Will this response make PAL_AMUD_updateUD step against UD.direction?
function rev = will_reverse(UD, resp)
    trial = length(UD.x);
    dir = UD.direction;
    if trial == 1
        rev = false;
        return;
    end
    noRevYet = max(UD.reversal) < 1;
    if resp == 1
        steps = (UD.d + 1 == UD.down) || noRevYet;
        rev = steps && dir == 1;
    else
        steps = (UD.u + 1 == UD.up) || noRevYet;
        rev = steps && dir == -1;
    end
end

% ---------------------------------------------------------------------------

function bad = compare_pm()
    alphas = -2:0.1:2;
    lbetas = -0.5:0.1:1;
    stims = -2:0.2:2;
    truth = [-0.4 10^0.4 0.5 0.03];
    cases = {
        'Psi',                 0.02,        [], 60
        'Psi-marginal (lapse)', 0:0.02:0.08, 4,  60
    };
    fprintf('\nPart 2: psy_quest %s against PAL_AMPM\n\n', psy_quest('version'));
    fprintf('%-22s %6s %10s %6s %10s %6s %12s %10s\n', 'case', 'trials', 'identical', 'ties', ...
            'max gap', 'diffs', 'max |dpost|', 'first diff');
    bad = false;
    for c = 1:size(cases, 1)
        [name, lambdas, marg, n] = cases{c, :};
        args = {'priorAlphaRange', alphas, 'priorBetaRange', lbetas, 'priorGammaRange', 0.5, ...
                'priorLambdaRange', lambdas, 'stimRange', stims, 'PF', @PAL_Gumbel};
        if ~isempty(marg), args = [args, {'marginalize', marg}]; end %#ok<AGROW>
        PM = PAL_AMPM_setupPM(args{:});

        d = struct();
        d.stim = {stims};
        d.param = {alphas, 10 .^ lbetas, 0.5, lambdas};
        d.pf = 'gumbel';
        d.stop_trials = n + 1;
        if ~isempty(marg)
            nu = zeros(1, 4); nu(marg) = 1;
            d.nuisance = nu;
        end
        h = psy_quest('open', d);

        state = uint64(3000 + c);
        same = 0; ties = 0; gap = 0; diffs = 0; maxdp = 0; first = 0;
        for t = 1:n
            ip = psy_quest('next', h);
            im = find(abs(stims - PM.xCurrent) < 1e-12, 1);
            if ip == im
                same = same + 1;
            else
                [~, ee] = PAL_AMPM_expectedEntropy(PM);
                ee = squeeze(ee);
                if abs(ee(ip) - ee(im)) <= 1e-9
                    ties = ties + 1;
                    gap = max(gap, abs(ee(ip) - ee(im)));
                else
                    diffs = diffs + 1;
                    if first == 0, first = t; end
                end
            end
            [u, state] = psy_trials('splitmix', state);
            pc = PAL_Gumbel(truth, PM.xCurrent);
            r = double(u < pc);
            PM = PAL_AMPM_updatePM(PM, r);
            psy_quest('update', h, im, r);
            post = psy_quest('posterior', h);
            maxdp = max(maxdp, max(abs(post(:) - PM.pdf(:))));
        end
        psy_quest('close', h);
        fd = '-';
        if first > 0, fd = sprintf('%d', first); end
        fprintf('%-22s %6d %10d %6d %10.2g %6d %12.3g %10s\n', name, n, same, ties, gap, diffs, maxdp, fd);
        bad = bad || diffs > 0 || maxdp > 1e-6;
    end
    fprintf('\n');
end
