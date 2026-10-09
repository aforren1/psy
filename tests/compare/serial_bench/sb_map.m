function h = sb_map(F, dev)
%SB_MAP  Board time (unwrapped us) to host time (us) with the fit F of sb_fit.
    h = F.y0 + F.slope * (dev - F.x0);
end
