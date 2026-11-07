# Static exact engines: omega (time, s)

ours = our branch-and-bound (search time); external = solver wall time incl. reading the binary file, excl. writing it. Single thread, Apple M-series, 16 GB.
kPlexS is not exact at k = 1 (not run); MC-BRB / PMC are clique solvers (k = 1 only); kPlexT needs k >= 2. `>7200` = killed after 2 h.

| graph | k | ours | mcbrb | pmc | kplexs | kplext | kpex | agree |
|---|---|---|---|---|---|---|---|---|
| email-Enron | 1 | 20 (0.03) | 20 (0.0151) | 20 (0.0863) |  |  |  | yes |
| email-Enron | 2 | 22 (0.0385) |  |  | 22 (0.034) | 22 (0.034) | 22 (0.024) | yes |
| email-Enron | 3 | 24 (0.0402) |  |  | 24 (0.0327) | 24 (0.0321) | 24 (0.0182) | yes |
| ca-AstroPh | 1 | 57 (0.002) | 57 (0.0063) | 57 (0.0547) |  |  |  | yes |
| ca-AstroPh | 2 | 57 (0.0021) |  |  | 57 (0.0063) | 57 (0.0062) | 57 (0.0057) | yes |
| ca-AstroPh | 3 | 57 (0.0024) |  |  | 57 (0.0065) | 57 (0.0062) | 57 (0.0063) | yes |
| soc-Epinions1 | 1 | 23 (0.0954) | 23 (0.0418) | 23 (0.221) |  |  |  | yes |
| soc-Epinions1 | 2 | 28 (0.188) |  |  | 28 (0.399) | 28 (0.174) | 28 (0.0735) | yes |
| soc-Epinions1 | 3 | 32 (0.187) |  |  | 32 (0.328) | 32 (0.121) | 32 (0.0565) | yes |
| sx-mathoverflow | 1 | 30 (0.0597) | 30 (0.029) | 30 (0.107) |  |  |  | yes |
| sx-mathoverflow | 2 | 34 (0.233) |  |  | 34 (3.13) | 34 (1.12) | 34 (0.227) | yes |
| sx-mathoverflow | 3 | 39 (1.25) |  |  | 39 (8.81) | 39 (0.882) | 39 (0.115) | yes |
| sx-askubuntu | 1 | 23 (0.0415) | 23 (0.0236) | 23 (0.235) |  |  |  | yes |
| sx-askubuntu | 2 | 26 (0.0464) |  |  | 26 (0.0435) | 26 (0.0331) | 26 (0.0237) | yes |
| sx-askubuntu | 3 | 28 (0.0642) |  |  | 28 (0.0565) | 28 (0.0359) | 28 (0.0288) | yes |
| com-dblp.ungraph | 1 | 114 (0.0232) | 114 (0.0207) | 114 (0.409) |  |  |  | yes |
| com-dblp.ungraph | 2 | 114 (0.0253) |  |  | 114 (0.02) | 114 (0.0234) | 114 (0.0141) | yes |
| com-dblp.ungraph | 3 | 114 (0.0256) |  |  | 114 (0.023) | 114 (0.0215) | 114 (0.0157) | yes |
| com-youtube.ungraph | 1 | 17 (0.304) | 17 (0.141) | 17 (2.09) |  |  |  | yes |
| com-youtube.ungraph | 2 | 20 (0.654) |  |  | 20 (0.359) | 20 (0.283) | 20 (0.141) | yes |
| com-youtube.ungraph | 3 | 21 (0.97) |  |  | 21 (0.42) | 21 (0.264) | 21 (0.159) | yes |
| wiki-Talk | 1 | 26 (1.02) | 26 (0.468) | 26 (3.48) |  |  |  | yes |
| wiki-Talk | 2 | 32 (21.4) |  |  | 32 (1.3e+03) | 32 (139) | 32 (8.53) | yes |
| wiki-Talk | 3 | 36 (241) |  |  | – (>7200) | 36 (302) | 36 (5.76) | yes |
