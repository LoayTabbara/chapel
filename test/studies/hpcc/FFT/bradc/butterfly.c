#include <math.h>
#include <stdlib.h>
#include <stdio.h>

void btrfly(j, wk1r, wk1i, wk2r, wk2i, wk3r, wk3i, a, b, c, d)
  int j;
  double wk1r, wk1i, wk2r, wk2i, wk3r, wk3i, *a, *b, *c, *d;
{ double x0r = a[j    ] + b[j    ];
  double x0i = a[j + 1] + b[j + 1];
  double x1r = a[j    ] - b[j    ];
  double x1i = a[j + 1] - b[j + 1];
  double x2r = c[j    ] + d[j    ];
  double x2i = c[j + 1] + d[j + 1];
  double x3r = c[j    ] - d[j    ];
  double x3i = c[j + 1] - d[j + 1];

  a[j    ] = x0r + x2r;
  a[j + 1] = x0i + x2i;
  x0r -= x2r;
  x0i -= x2i;
  c[j    ] = wk2r * x0r - wk2i * x0i;
  c[j + 1] = wk2r * x0i + wk2i * x0r;
  x0r = x1r - x3i;
  x0i = x1i + x3r;
  b[j    ] = wk1r * x0r - wk1i * x0i;
  b[j + 1] = wk1r * x0i + wk1i * x0r;
  x0r = x1r + x3i;
  x0i = x1i - x3r;
  d[j    ] = wk3r * x0r - wk3i * x0i;
  d[j + 1] = wk3r * x0i + wk3i * x0r;
}
