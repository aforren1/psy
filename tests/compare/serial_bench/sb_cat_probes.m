function P = sb_cat_probes(P1, P2)
%SB_CAT_PROBES  Join the echo probes taken before and after a stream, so the
%   clock fit spans the stream and its slope covers the board's drift.
    P.t0 = [P1.t0; P2.t0];
    P.t2 = [P1.t2; P2.t2];
    P.rx = [P1.rx; P2.rx];
    P.tx = [P1.tx; P2.tx];
    P.ok = [P1.ok; P2.ok];
end
