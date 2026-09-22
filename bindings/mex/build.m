function build(name)
%BUILD  Compile the psy MEX bindings (MATLAB or Octave).
%
%   build()             builds every psy_*.c in this directory
%   build('parallel')   builds only psy_parallel.c
%
%   Run it from any directory:  run('.../bindings/mex/build.m')
%   Each MEX file is written next to its source as psy_<name>.<mexext>.
%
%   Octave needs the development package (e.g. `octave-dev`) for `mex`.
%   MATLAB needs a configured C compiler (`mex -setup`).

    here = fileparts(mfilename('fullpath'));
    root = fullfile(here, '..', '..');           % repo root holds the headers

    if nargin < 1
        d = dir(fullfile(here, 'psy_*.c'));
        srcs = {d.name};
    else
        srcs = {sprintf('psy_%s.c', name)};
    end
    if isempty(srcs)
        error('psy:build', 'no psy_*.c sources found in %s', here);
    end

    % Per-source platform gates, mirroring PSY_PLATFORMS_<lib> in
    % CMakeLists.txt. A header #errors on a platform it does not support,
    % which would fail the whole build; skip it with a message instead.
    unsupported = {'psy_parallel.c', ismac};

    % mex writes its output to the current directory; build next to the
    % sources so the result lands in bindings/mex regardless of the caller's cwd.
    old = cd(here);
    cleanup = onCleanup(@() cd(old));   %#ok<NASGU>

    for i = 1:numel(srcs)
        k = find(strcmp(unsupported(:, 1), srcs{i}), 1);
        if ~isempty(k) && unsupported{k, 2}
            fprintf('skipping %s: not supported on this platform\n', srcs{i});
            continue;
        end
        args = {['-I' root], srcs{i}};
        if ispc
            % Windows async-pulse workers use Win32 threads, so no thread
            % library. psy_serial.h does need SetupAPI and the registry for
            % port enumeration, and its #pragma comment(lib) reaches MSVC
            % only: MinGW Octave and MATLAB's MinGW-w64 add-on would fail to
            % link with undefined SetupDi*. mex takes -l<name> on both
            % toolchains (it becomes <name>.lib for MSVC), so one spelling
            % covers them.
            if strcmp(srcs{i}, 'psy_serial.c')
                args(end+1:end+2) = {'-lsetupapi', '-ladvapi32'};   %#ok<AGROW>
            end
        else
            % The async-pulse workers use pthreads on Linux/macOS.
            args{end+1} = '-lpthread';   %#ok<AGROW>
        end
        mex(args{:});
        fprintf('built %s (%s) in %s\n', srcs{i}, mexext, here);
    end
end
