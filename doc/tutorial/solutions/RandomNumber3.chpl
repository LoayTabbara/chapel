var seed = 1;
const multiplier = 16807,
      modulus = 2147483647;

def RandomNumber (x_n) {
  // The following calculation must be done in at least 46-bit arithmetic!
  return x_n:int(64) * multiplier % modulus;
}

def RealRandomNumber () {
  // Cast RandomNumber's return value to whatever seed's type is
  seed = RandomNumber(seed) : seed.type;
  return (seed-1) / (modulus-2) : real;
}

config const numberOfIterations = 10000;

for i in 1..numberOfIterations do
  // Cast RandomNumber's return value to whatever seed's type is
  seed = RandomNumber(seed) : seed.type;

writeln ("After ", numberOfIterations, " iterations, RandomNumber returns ", seed);
writeln ("RealRandomNumber returns ", RealRandomNumber());
