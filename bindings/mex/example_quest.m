% example_quest.m - psy_quest MEX demo (MATLAB / Octave)
%
% Psi-marginal (Prins 2013): threshold and slope estimated, the lapse rate a
% nuisance axis, against a simulated 2AFC observer. The first run is the plain
% loop; the second runs the inference on the async thread and polls it as a
% frame loop would. Build first (run build.m).

rng(2);
truth = [-1.5 3.0 0.5 0.03];               % threshold, slope, guess, lapse

d = struct();
d.stim  = {{-3, 0, 31}};                   % log10 contrast, a linspace
d.param = {{-3, 0, 61}, {0.5, 6, 12}, 0.5, [0 0.02 0.04 0.06]};
d.nuisance = [0 0 0 1];                    % marginalize the lapse out of the selection
d.stop_trials = 80;

h = psy_quest('open', d);
while ~psy_quest('done', h)
    i = psy_quest('next', h);                            % 1-based grid index
    k = psy_quest('simulate', h, i, truth, rand());      % the simulated response
    psy_quest('update', h, i, k);
end
est = psy_quest('estimate', h, 'mean');
fprintf('psy_quest %s, plain loop: threshold %.3f (truth %.2f), slope %.2f, sd %.3f\n', ...
        psy_quest('version'), est(1), truth(1), est(2), psy_quest('sd', h, 1));
psy_quest('close', h);

% The same design with the inference on a C thread. While it runs the thread
% owns the Quest, so the observer is simulated here from the snapshot.
pc = @(x) truth(3) + (1 - truth(3) - truth(4)) * (1 - exp(-10 ^ (truth(2) * (x - truth(1)))));
h = psy_quest('open', d);
psy_quest('async_start', h, struct('below_normal', true));
fprintf('async thread policy: %s\n', psy_quest('async_policy', h));
snap = psy_quest('async_poll', h);                       % seq 0: the first stimulus
late = 0;
while ~snap.done
    k = double(rand() < pc(snap.stim(1)));               % the trial
    seq = psy_quest('async_submit', h, snap.proposed, k);
    snap = psy_quest('async_poll', h);                   % a frame loop polls ...
    while snap.seq < seq
        late = late + 1;
        pause(0.001);                                    % ... and draws a frame
        snap = psy_quest('async_poll', h);
    end
end
psy_quest('async_stop', h);                              % the Quest is ours again
est = psy_quest('estimate', h, 'mean');
fprintf('async loop: %d trials, threshold %.3f, %d polls found the proposal late\n', ...
        psy_quest('n_trials', h), est(1), late);
psy_quest('close', h);
