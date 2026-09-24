function compare_quest_mquestplus(mqp_dir)
%COMPARE_QUEST_MQUESTPLUS  psy_quest (MEX) against mQUESTPlus, cell by cell.
%
%   compare_quest_mquestplus('/path/to/mQUESTPlus')
%   compare_quest_mquestplus            % reads the MQUESTPLUS_DIR variable
%
%   mQUESTPlus is BrainardLab/mQUESTPlus (MIT). Get it with
%       git clone https://github.com/BrainardLab/mQUESTPlus
%   and build the MEX first (run bindings/mex/build.m).
%
%   Runs the examples of qpQuestPlusPaperSimpleExamplesDemo (Watson 2017
%   figures 2, 3 and 4) and qpQuestPlusMarginalizeDemo on both, on the same
%   grids. mQUESTPlus drives: on every trial both select, the selections are
%   compared, then both are updated with mQUESTPlus's stimulus and one outcome
%   drawn from a fixed stream, and the posteriors are compared cell by cell.
%   A selection that differs is a TIE when mQUESTPlus's own expected entropy
%   at our stimulus is within 1e-9 bits of its minimum (psy_quest's default
%   tie_tolerance), and a DIFFERENCE otherwise.
%
%   The 'max gap' column is the largest such tie, in bits.
%
%   Prints one row per example and raises an error on any difference, or on a
%   posterior difference above 1e-6 (the float table's precision).
%
%   Outcome coding: mQUESTPlus numbers outcomes 1..K, and its PFs put the
%   incorrect response first; psy_quest's are 0..K-1 with 1 = correct. So
%   psy outcome = mQUESTPlus outcome - 1 throughout.
%
%   The Weibull of mQUESTPlus (qpPFWeibull) is QUEST+'s form in dB,
%   F = 1 - exp(-10^(beta (x - alpha) / 20)); psy_quest's PSYQ_PF_GUMBEL is
%   F = 1 - exp(-10^(beta (x - alpha))), so the psy slope axis is beta / 20.
%   The normal example runs mQUESTPlus's own qpPFNormal through pf_batch.

    if nargin < 1 || isempty(mqp_dir)
        mqp_dir = getenv('MQUESTPLUS_DIR');
    end
    if isempty(mqp_dir) || ~exist(fullfile(mqp_dir, 'questplus', 'qpInitialize.m'), 'file')
        error('compare:setup', 'give the mQUESTPlus directory (or set MQUESTPLUS_DIR)');
    end
    root = fileparts(fileparts(fileparts(mfilename('fullpath'))));
    addpath(fullfile(root, 'bindings', 'mex'));
    addpath(genpath(mqp_dir));
    if exist('OCTAVE_VERSION', 'builtin')
        % mQUESTPlus calls nansum, which Octave keeps in the statistics package.
        pkg load statistics
    end

    cases = {};
    % Figure 2: threshold only.
    cases{end+1} = struct('name', 'fig2 threshold', 'seed', 2002, 'n', 32, ...
        'stim', -40:0, 'psi', {{-40:0, 3.5, 0.5, 0.02}}, 'pf', 'weibull', ...
        'truth', [-20 3.5 0.5 0.02], 'marg', []);
    % Figure 3: threshold, slope and lapse.
    cases{end+1} = struct('name', 'fig3 thr/slope/lapse', 'seed', 2004, 'n', 200, ...
        'stim', -40:0, 'psi', {{-40:0, 2:5, 0.5, 0:0.01:0.04}}, 'pf', 'weibull', ...
        'truth', [-20 3 0.5 0.02], 'marg', []);
    % Figure 4: normal mean, sd and lapse.
    cases{end+1} = struct('name', 'fig4 normal', 'seed', 2008, 'n', 128, ...
        'stim', -10:10, 'psi', {{-5:5, 1:10, 0:0.01:0.04}}, 'pf', 'normal', ...
        'truth', [1 3 0.02], 'marg', []);
    % qpQuestPlusMarginalizeDemo: slope and lapse marginalized out. The
    % nuisance axis in the middle makes psy_quest permute its grid.
    cases{end+1} = struct('name', 'marginalize [2 4]', 'seed', 2004, 'n', 64, ...
        'stim', -40:0, 'psi', {{-40:0, 1:5, 0.5, 0:0.01:0.1}}, 'pf', 'weibull', ...
        'truth', [-18 3 0.5 0.04], 'marg', [2 4]);

    fprintf('\npsy_quest %s against mQUESTPlus (%s)\n\n', psy_quest('version'), mqp_dir);
    fprintf('%-22s %6s %10s %6s %10s %6s %12s %10s\n', 'example', 'trials', 'identical', 'ties', ...
            'max gap', 'diffs', 'max |dpost|', 'first diff');
    bad = false;
    for c = 1:numel(cases)
        r = run_case(cases{c});
        fd = '-';
        if r.first_diff > 0, fd = sprintf('%d', r.first_diff); end
        fprintf('%-22s %6d %10d %6d %10.2g %6d %12.3g %10s\n', cases{c}.name, r.n, r.same, r.ties, ...
                r.maxgap, r.diffs, r.maxdp, fd);
        bad = bad || r.diffs > 0 || r.maxdp > 1e-6;
    end
    fprintf('\n');
    if bad
        error('compare:disagree', 'psy_quest and mQUESTPlus disagree beyond tolerance');
    end
    fprintf('agreement within tolerance on every example\n');
end

function r = run_case(c)
    state = uint64(c.seed);
    np = numel(c.psi);
    % mQUESTPlus side.
    if strcmp(c.pf, 'weibull')
        qpPF = @qpPFWeibull;
    else
        qpPF = @qpPFNormal;
    end
    args = {'stimParamsDomainList', {c.stim}, 'psiParamsDomainList', c.psi, 'qpPF', qpPF};
    if ~isempty(c.marg), args = [args, {'marginalize', c.marg}]; end
    qd = qpInitialize(args{:});

    % psy_quest side, same grids.
    d = struct();
    d.stim = {c.stim};
    d.stop_trials = c.n + 1;
    if strcmp(c.pf, 'weibull')
        p = c.psi;
        p{2} = p{2} / 20;                           % dB slope -> log10 slope
        d.param = p;
        d.pf = 'gumbel';
    else
        d.param = c.psi;
        d.pf_batch = @(s, P) qpPFNormal(repmat(s(1), size(P, 1), 1), P);
    end
    if ~isempty(c.marg)
        nu = zeros(1, np);
        nu(c.marg) = 1;
        d.nuisance = nu;
    end
    h = psy_quest('open', d);
    cleanup = onCleanup(@() psy_quest('close', h));

    r = struct('n', c.n, 'same', 0, 'ties', 0, 'maxgap', 0, 'diffs', 0, 'maxdp', 0, 'first_diff', 0);
    for t = 1:c.n
        stim = qpQuery(qd);
        im = find(qd.stimParamsDomain == stim, 1);
        ip = psy_quest('next', h);
        if ip == im
            r.same = r.same + 1;
        else
            e = qd.expectedNextEntropiesByStim;
            if abs(e(ip) - e(im)) <= 1e-9
                r.ties = r.ties + 1;
                r.maxgap = max(r.maxgap, abs(e(ip) - e(im)));
            else
                r.diffs = r.diffs + 1;
                if r.first_diff == 0, r.first_diff = t; end
            end
        end
        % One outcome from the fixed stream, at mQUESTPlus's stimulus.
        [u, state] = psy_trials('splitmix', state);
        pk = qpPF(stim, c.truth);                  % [P(outcome 1) P(outcome 2)]
        out = 1 + (u >= pk(1));
        qd = qpUpdate(qd, stim, out);
        psy_quest('update', h, im, out - 1);
        post = psy_quest('posterior', h);
        v = reshape(permute(post, np:-1:1), [], 1);   % C order: last axis fastest
        r.maxdp = max(r.maxdp, max(abs(v - qd.posterior)));
    end
end
