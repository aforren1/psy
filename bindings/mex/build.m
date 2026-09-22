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
        if ~ispc
            % psy_parallel's async-pulse worker uses pthreads on Linux/macOS.
            % On Windows it uses Win32 threads, so no extra link library.
            args{end+1} = '-lpthread';   %#ok<AGROW>
        end
        mex(args{:});
        fprintf('built %s (%s) in %s\n', srcs{i}, mexext, here);
    end
end
