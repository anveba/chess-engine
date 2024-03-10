#ifndef RNG_H_INCLUDED
#define RNG_H_INCLUDED

#include <cstdint>

// References: https://prng.di.unimi.it/
//             https://en.wikipedia.org/wiki/Xorshift

class Xshiro256
{
  public:
    inline Xshiro256(uint64_t s0, uint64_t s1, uint64_t s2, uint64_t s3)
    {
        s[0] = s0;
        s[1] = s1;
        s[2] = s2;
        s[3] = s3;
    }

    inline uint64_t next()
    {
        const uint64_t result = rotl(s[0] + s[3], 23) + s[0];

        const uint64_t t = s[1] << 17;

        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];

        s[2] ^= t;

        s[3] = rotl(s[3], 45);

        return result;
    }

  private:
    constexpr uint64_t rotl(const uint64_t x, int k) const
    {
        return (x << k) | (x >> (64 - k));
    }

    uint64_t s[4];
};

class Xorshift64 // xorshift
{
  public:
    inline Xorshift64(uint64_t state)
        : state(state)
    {
    }

    inline uint64_t next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

  private:
    uint64_t state;
};

class Splitmix64
{

  public:
    inline Splitmix64(uint64_t state)
        : state(state)
    {
    }

    inline uint64_t next()
    {
        uint64_t z = (state += 0x9e3779b97f4a7c15);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
        z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
        return z ^ (z >> 31);
    }

  private:
    uint64_t state;
};

#endif