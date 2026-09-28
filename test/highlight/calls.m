y = max (2, 5);
#   ^ function.call
y = x(2);
#   ^ variable
y = obj.method (x);
#       ^ function.method.call
y = [2 sqrt(sum (v, 2))];
#      ^ variable
#           ^ function.call
y = [1 (max (2, 5))];
#       ^ function.call
c = {@(t) abs (t), 2};
#         ^ function.call
y = c{f (x)};
#     ^ function.call
