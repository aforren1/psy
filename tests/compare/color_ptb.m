function color_ptb(ptb_dir)
%COLOR_PTB  ysp_color (MEX) against Psychtoolbox's colorimetric code.
%
%   color_ptb('/path/to/Psychtoolbox')
%   color_ptb                 % reads the PSYCHTOOLBOX_DIR variable
%
%   Build the MEX first (run bindings/mex/build.m). Psychtoolbox is not
%   vendored; pass the folder that holds PsychColorimetric and PsychCal.
%
%   1. ComputeDKL_M: the DKL matrix at three backgrounds of B_monitor, with
%      the standard luminance (T_Y = 0.68990272 L + 0.34832189 M) and with
%      a participant's (0.75 L + 0.30 M, ysp_color's 'lum_stated').
%   2. LMSToMacBoyn with T_cones_ss2 and T_CIE_Y2 (CIE 170-2:2015): l, s
%      and the luminance of 500 colors. Psychtoolbox fits the luminance
%      weights to T_CIE_Y2; ysp_color takes the published ones.
%   3. SensorToSettings: Psychtoolbox's PTB3TestCal resampled to 1 nm,
%      the same spectra and ambient in a ysp_color calibration; LMS of 500
%      colors (some outside the gamut) to primaries (SensorToPrimary) and
%      the out-of-gamut flags (badIndex) against ysp_color's 'in'. The
%      settings themselves go through each toolbox's own gamma inversion
%      and are not compared.
%   4. MaximizeGamutContrast against ysp_color's max_scale, symmetric, for
%      500 directions and backgrounds in primary coordinates.
%
%   Prints a table and raises an error on any disagreement beyond the
%   stated tolerance.

    if nargin < 1 || isempty(ptb_dir)
        ptb_dir = getenv('PSYCHTOOLBOX_DIR');
    end
    if isempty(ptb_dir) || ~exist(fullfile(ptb_dir, 'PsychColorimetric', 'ComputeDKL_M.m'), 'file')
        error('compare:setup', 'give the Psychtoolbox folder that holds PsychColorimetric (or set PSYCHTOOLBOX_DIR)');
    end
    root = fileparts(fileparts(fileparts(mfilename('fullpath'))));
    addpath(fullfile(root, 'bindings', 'mex'));
    addpath(fullfile(ptb_dir, 'PsychColorimetric'), fullfile(ptb_dir, 'PsychCal'), ...
            fullfile(ptb_dir, 'PsychColorimetricData', 'PsychColorimetricMatFiles'), ...
            fullfile(ptb_dir, 'PsychCalDemoData'), fullfile(ptb_dir, 'PsychOneliners'));
    rng(20261006);
    bad = false;
    fprintf('ysp_color %s against Psychtoolbox (%s), MATLAB %s\n', ysp_color('version'), ptb_dir, version);
    fprintf('%-58s %12s %10s\n', 'check', 'measured', 'tolerance');
    report = @(name, v, tol) fprintf('%-58s %12.3g %10.1g %s\n', name, v, tol, ternary(v <= tol, '', 'FAIL'));

    % ---- 1. ComputeDKL_M
    load('B_monitor', 'B_monitor', 'S_monitor');
    load('T_cones_ss2', 'T_cones_ss2', 'S_cones_ss2');
    cal = bm_cal(B_monitor, S_monitor);
    lum = ysp_color('lum_stated', [0.75 0.30 0]);
    worst = [0 0];
    for bg = [0.4 0.5 0.3; 0.5 0.5 0.5; 0.2 0.6 0.7]'
        for which = 1:2
            d = struct('cal', cal, 'background', bg');
            if which == 1, w = [0.68990272 0.34832189]; else, w = [0.75 0.30]; d.lum = lum; end
            h = ysp_color('open', d);
            info = ysp_color('info', h);
            ysp_color('close', h);
            T_Y = w(1) * T_cones_ss2(1, :) + w(2) * T_cones_ss2(2, :);
            M = ComputeDKL_M(info.bg_lms', T_cones_ss2, T_Y);
            worst(which) = max(worst(which), max(abs(info.dkl(:) - M(:)) ./ max(abs(M(:)), 1e-12)));
        end
    end
    report('ComputeDKL_M, standard luminance: max relative diff', worst(1), 1e-9); bad = bad || worst(1) > 1e-9;
    report('ComputeDKL_M, a participant''s 0.75 L + 0.30 M', worst(2), 1e-9); bad = bad || worst(2) > 1e-9;

    % ---- 2. LMSToMacBoyn
    load('T_CIE_Y2', 'T_CIE_Y2');
    h = ysp_color('open', struct('cal', cal, 'background', [0.5 0.5 0.5]));
    RGB = 0.02 + 0.96 * rand(500, 3);
    LMS = ysp_color('convert', h, 'rgb', 'lms', RGB);
    MB = ysp_color('convert', h, 'rgb', 'mb', RGB);
    [ls, factors] = LMSToMacBoyn(LMS', T_cones_ss2, T_CIE_Y2, 1);
    e_l = max(abs(MB(:, 1) - ls(1, :)'));
    e_s = max(abs(MB(:, 2) - ls(2, :)') ./ ls(2, :)');
    e_v = max(abs(MB(:, 3) - ls(3, :)') ./ ls(3, :)');
    fprintf('  Psychtoolbox''s fitted weights %.8f %.8f; ysp_color''s 0.68990272 0.34832189\n', factors(1), factors(2));
    report('LMSToMacBoyn l: max abs diff', e_l, 1e-6); bad = bad || e_l > 1e-6;
    report('LMSToMacBoyn s: max relative diff', e_s, 1e-6); bad = bad || e_s > 1e-6;
    report('LMSToMacBoyn luminance: max relative diff', e_v, 1e-6); bad = bad || e_v > 1e-6;
    ysp_color('close', h);

    % ---- 3. SensorToSettings: primaries and badIndex
    ptb = LoadCalFile('PTB3TestCal', [], fullfile(ptb_dir, 'PsychCalDemoData'));
    S1 = [380 1 401];
    P1 = SplineSpd(ptb.S_device, ptb.P_device, S1);
    A1 = SplineSpd(ptb.S_ambient, ptb.P_ambient, S1);
    ptb.S_device = S1; ptb.P_device = P1; ptb.T_device = WlsToT(S1);
    ptb.S_ambient = S1; ptb.P_ambient = A1; ptb.T_ambient = WlsToT(S1);
    ptb = SetSensorColorSpace(ptb, T_cones_ss2, S_cones_ss2);
    ptb = SetGammaMethod(ptb, 0);
    readings = [-1 0 0 0 0; 0 1 1 0 0; 1 1 1 0 0; 2 1 1 0 0];
    % ysp_color's spectra are each gun at full output, the black's light
    % included; Psychtoolbox's P_device is the light above the ambient.
    pcal = ysp_color('cal_derive', readings, [(380:780)' (P1(:, 1:3) + A1) A1]);
    h = ysp_color('open', struct('cal', pcal, 'background', [0.5 0.5 0.5]));
    RGB = -0.15 + 1.3 * rand(500, 3);                     % some outside the gamut
    LMS = ysp_color('convert', h, 'rgb', 'lms', RGB);     % absolute, ambient included
    prim_ptb = SensorToPrimary(ptb, LMS');
    [rgb_ysp, g] = ysp_color('to_rgb', h, 'lms', LMS);
    [~, badIndex] = SensorToSettings(ptb, LMS');
    % ysp_color stores the spectra as float (the .yspcal file), Psychtoolbox
    % keeps double: the difference is in units of a gun's full output
    e_p = max(abs(prim_ptb(:) - reshape(rgb_ysp', [], 1)));
    if numel(badIndex) == 500, flags_ptb = logical(badIndex(:)); else, flags_ptb = false(500, 1); flags_ptb(badIndex) = true; end
    near = any(abs(rgb_ysp) < 1e-5 | abs(rgb_ysp - 1) < 1e-5, 2);   % on a face: either answer is right
    n_flag = sum(flags_ptb(~near) ~= ~g.in(~near));
    fprintf('  %d of 500 out of gamut by Psychtoolbox, %d by ysp_color\n', sum(flags_ptb), sum(~g.in));
    report('SensorToPrimary against LMS to RGB: max abs diff', e_p, 1e-7); bad = bad || e_p > 1e-7;
    report('SensorToSettings badIndex against ysp_color in: differ', n_flag, 0); bad = bad || n_flag > 0;
    ysp_color('close', h);

    % ---- 4. MaximizeGamutContrast
    worst = 0;
    for i = 1:500
        white = 0.05 + 0.9 * rand(3, 1);
        dir = randn(3, 1);
        s = MaximizeGamutContrast(dir, white);
        h = ysp_color('open', struct('cal', cal, 'background', white'));
        k = ysp_color('max_scale', h, 'rgb', (white + dir)', 'symmetric');
        ysp_color('close', h);
        worst = max(worst, abs(k - s) / s);
    end
    report('MaximizeGamutContrast against max_scale: max relative diff', worst, 1e-12); bad = bad || worst > 1e-12;

    if bad
        error('compare:disagree', 'ysp_color and Psychtoolbox disagree beyond tolerance');
    end
    fprintf('agreement within tolerance on every check\n');
end

function cal = bm_cal(B, S)
    % A ysp_color calibration with B_monitor's spectra: one reading of 1 per
    % gun at full output, no chromaticities, no black.
    nm = (S(1):S(2):S(1) + S(2) * (S(3) - 1))';
    readings = [-1 0 0 0 0; 0 1 1 0 0; 1 1 1 0 0; 2 1 1 0 0];
    cal = ysp_color('cal_derive', readings, [nm B(:, 1:3)]);
end

function r = ternary(c, a, b)
    if c, r = a; else, r = b; end
end
