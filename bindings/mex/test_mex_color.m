function test_mex_color()
%TEST_MEX_COLOR  Key checks of the ysp_color MEX function. Runs in MATLAB and
%   in Octave. Prints PASS, or stops with the error of the first check that
%   failed. Build first (run build.m), then from this directory:
%   test_mex_color. tests/compare/color_ptb.m compares it with
%   Psychtoolbox.

    here = fileparts(mfilename('fullpath'));
    addpath(here);
    fprintf('ysp_color %s\n', ysp_color('version'));

    % a nominal display with Gaussian spectra, flagged NOMINAL
    nm = (380:780)';
    spd = [0.010 * exp(-0.5 * ((nm - 612) / 14).^2), 0.012 * exp(-0.5 * ((nm - 545) / 24).^2), ...
           0.014 * exp(-0.5 * ((nm - 455) / 12).^2)];
    xy = [0.64 0.33; 0.30 0.60; 0.15 0.06; 0.3127 0.3290];
    readings = [-1 0 0 0 0];
    for gun = 0:2
        for k = 1:8
            readings(end + 1, :) = [gun, k / 8, 0, xy(gun + 1, :)]; %#ok<AGROW>
        end
    end
    Yp = [21.26 71.52 7.22];
    for i = 2:size(readings, 1)
        readings(i, 3) = Yp(readings(i, 1) + 1) * readings(i, 2)^2.2;
    end
    cal = ysp_color('cal_derive', readings, [nm spd], true);
    info = ysp_color('cal_info', cal);
    check(isa(cal, 'uint8') && numel(cal) == 62528, 'calibration bytes');
    check(bitand(info.flags, 8) ~= 0 && ~isempty(strfind(info.describe, 'datasheet')), 'nominal spectra flagged');
    check(size(info.lut, 1) == 4096 && info.lut(1, 1) == 0 && info.lut(end, 1) == 1, 'CLUT');

    h = ysp_color('open', struct('cal', cal, 'background', [0.5 0.5 0.5]));
    check(~isempty(strfind(ysp_color('describe', h), 'relative to the display')), 'describe names the D65 choice');

    % round trips through every space
    names = {'rgb', 'device', 'xyz', 'xyy', 'cielab', 'cielch', 'cieluv', 'cielchuv', 'oklab', 'oklch', ...
             'srgb', 'display-p3', 'rec2020', 'lms', 'cone', 'dkl', 'dkl-cart', 'mb'};
    RGB = [0.1 0.2 0.3; 0.9 0.5 0.05; 0.5 0.5 0.5; 0.33 0.71 0.66];
    for i = 1:numel(names)
        X = ysp_color('convert', h, 'rgb', names{i}, RGB);
        [back, g] = ysp_color('to_rgb', h, names{i}, X);
        check(max(abs(back(:) - RGB(:))) < 1e-12, 'round trip through %s', names{i});
        check(all(g.in), 'in gamut through %s', names{i});
    end

    % a DKL direction, its gamut and the largest contrast
    k = ysp_color('max_scale', h, 'dkl', [0 90 1], 'symmetric');
    [~, g] = ysp_color('to_dir', h, 'dkl', [0 90 0.999 * k; 0 90 1.01 * k]);
    check(g.in(1) && ~g.in(2) && g.distance(2) > 0, 'max_scale is the gamut edge');
    r = ysp_color('ring', h, 'dkl', 0, 8, 'symmetric');
    check(abs(r(3) - k) < 1e-15 && abs(r(1) - r(5)) < 1e-12, 'ring');
    [m, g] = ysp_color('map', h, 'oklch', [0.7 0.3 150], 'chroma_oklch');
    [~, g2] = ysp_color('to_rgb', h, 'rgb', m);
    check(~g.in && g.kept > 0 && g.kept < 1 && g2.in, 'map');

    % refusals carry their id
    lumo = ysp_color('cal_derive', [-1 0 0.2 0 0; 0 1 20 0 0; 1 1 60 0 0; 2 1 8 0 0]);
    h2 = ysp_color('open', struct('cal', lumo, 'background', [0.5 0.5 0.5]));
    expect_error('ysp_color:refused', @() ysp_color('to_rgb', h2, 'cielab', [50 0 0]));
    expect_error('ysp_color:refused', @() ysp_color('to_rgb', h2, 'dkl', [0 90 0.1]));
    expect_error('ysp_color:arg', @() ysp_color('to_rgb', h2, 'lab', [50 0 0]));
    expect_error('ysp_color:range', @() ysp_color('to_rgb', h2, 'device', [1.5 0 0]));
    Y = ysp_color('convert', h2, 'device', 'rgb', [0.5 0.5 0.5; 1.5 0 0]);
    check(all(isnan(Y(2, :))) && ~any(isnan(Y(1, :))), 'a row out of range is NaN in a batch');
    ysp_color('close', h2);

    % a participant's luminance from one null of a synthetic observer
    w = [0.75 0.30 0];
    M = ysp_color('info', h).rgb_to_lms;
    u = [0.08 -0.06 0.01]'; v = [0.03 0.03 0.03]';
    t = -(w * M * u) / (w * M * v);
    d = (u + t * v)';
    lum = ysp_color('lum_derive', [0.5 + d, 0.5 - d], cal, struct('participant', 'P01', 'method', 'hfp'));
    li = ysp_color('lum_info', lum);
    check(max(abs(li.w / norm(li.w) - w / norm(w))) < 1e-9, 'luminance fit');
    h3 = ysp_color('open', struct('cal', cal, 'background', [0.5 0.5 0.5], 'lum', lum));
    lm = ysp_color('to_dir', h3, 'dkl-cart', [0 0.1 0]);
    check(abs(w * M * lm') < 1e-12 * norm(M * lm'), 'the L-M axis is isoluminant for the participant');
    [a, b] = ysp_color('lum_pair', h3, 0, 10, 0.05);
    check(max(abs((a - 0.5) + (b - 0.5))) < 1e-15, 'lum_pair');
    ysp_color('close', h3);

    codes = ysp_color('output_code', cal, [0 0.5 1; -1 2 0.25]);
    check(isequal(codes(1, [1 3]), [0 255]) && isequal(codes(2, 1:2), [0 255]), 'output_code');
    ysp_color('close', h);
    expect_error('ysp_color:handle', @() ysp_color('describe', h));
    fprintf('PASS\n');
end

function check(cond, msg, varargin)
    if ~cond
        error('test_mex_color:fail', ['FAIL: ' msg], varargin{:});
    end
end

function expect_error(id, f)
    try
        f();
    catch e
        if ~strcmp(e.identifier, id)
            error('test_mex_color:fail', 'FAIL: expected %s, got %s: %s', id, e.identifier, e.message);
        end
        return;
    end
    error('test_mex_color:fail', 'FAIL: expected %s, nothing was raised', id);
end
