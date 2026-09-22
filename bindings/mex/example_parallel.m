% example.m - psy_parallel MEX demo (MATLAB / Octave)
%
% Build first:   run build.m   (produces psy_parallel.<mexext>)
% Then run this script. On Linux see the BACKENDS section of psy_parallel.h for ppdev access.

code = 42;

ports = psy_parallel('list');
fprintf('%d parallel port(s) detected\n', numel(ports));
for i = 1:numel(ports)
    fprintf('  %s (backend %d, base 0x%X)\n', ...
            ports(i).name, ports(i).backend, ports(i).base_addr);
end

% Platform defaults: ppdev /dev/parport0 (Linux), inpout @ LPT1 (Windows).
% For raw x86 I/O:   h = psy_parallel('open', 'direct', 888);
% For a specific device: h = psy_parallel('open', '/dev/parport1');
h = psy_parallel('open');
cleanup = onCleanup(@() psy_parallel('close', h));   %#ok<NASGU>

psy_parallel('pulse', h, code, 2000);      % blocking 2 ms trigger
fprintf('sent trigger %d (2 ms blocking pulse)\n', code);

psy_parallel('pulseasync', h, code, 2000); % non-blocking; worker drops it
fprintf('queued async trigger %d\n', code);

s = psy_parallel('status', h);
fprintf('status = %d\n', s);
