function sb_csv(path, names, M)
%SB_CSV  Write a numeric matrix with a header line; LF line endings on
%   every platform (fopen 'w', not 'wt'). NaN is written as an empty field.
    fid = fopen(path, 'w');
    if fid < 0, error('serial_bench:csv', 'cannot write %s', path); end
    c = onCleanup(@() fclose(fid));
    fprintf(fid, '%s\n', strjoin(names, ','));
    for i = 1:size(M, 1)
        f = cell(1, size(M, 2));
        for j = 1:size(M, 2)
            if isnan(M(i, j)), f{j} = ''; else, f{j} = sprintf('%.10g', M(i, j)); end
        end
        fprintf(fid, '%s\n', strjoin(f, ','));
    end
end
