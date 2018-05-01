# Hard Drives

While Hard Disk Drives during normal operation behave deterministically –
data goes in, same data goes out – when they start to fail, that determinism
is not retained.

When HDD is saving data, it performs two operations, first it encodes it
using something like
[8b/10b encoding](https://en.wikipedia.org/wiki/8b/10b_encoding) to ensure
clock recovery on reading – knowing when one byte starts and the other ends.
Secondly in calculates an ECC for the whole
[sector](https://en.wikipedia.org/wiki/Disk_sector) (512B or 4096B)
and saves it together with the data.

This allows the disk to automatically and transparently detect or correct
small read errors and detect large read errors.

Because read errors are a natural consequence of the tight tolerances of
hard disks, HDDs implement a mechanism that will cause a read retry on an
error. This will cause the read to stall until the sector rotates under the
read/write head again where the process of reading repeats.
If the state of the platter is really bad, the disk may need multiple read
attempts to achieve successful read.

Only if multiple re-reads fail, the disk will return a read error to operating
system.

## Rolling dice

The problem with re-reading the same sector over and over is that every time
sector is read, the disk implicitly trusts the associated ECC data – this is
how the disk determines if the read is good or bad – by applying the ECC
algorithm on the data and checking if it can be recovered from that ECC
information.

If both the data and the
[50 or 100B of ECC](https://www.seagate.com/tech-insights/advanced-format-4k-sector-hard-drives-master-ti/)
is sufficiently damaged, the read may succeed _even if the returned data
is nothing like what was originally written there_.

While probabilities of that happening are small, bugs in firmware may
increase them significantly.

## SMART data

Statistics from [Google](https://research.google.com/pubs/pub32774.html)
indicate that hard drives indicating scan errors have a 12 to 35% probability
of failure and disks that report reallocations have a 7 to 20% probability of
failure during their lifetime, compared to under 5% and around 3% respectively
for drives that do not.

This and other conclusions from the paper indicate that while SMART failures
are very good indicator that the drive is likely to fail, lack of SMART
failures is _not_ a good indicator that the drive will continue operating
correctly.

# Early problem detection

So, to detect if the disk is likely to fail early, application needs to
interpret probabilistic data (the time to read a sector) that depends on
how "lucky" the drive was with reading the sector.

(Empirically, I found sectors that in half of the reads didn't require
re-reading, but in more than 10% of reads required between 3 and 13 attempts
from the drive to read.)

But, what the
[NetApp](http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.63.1412&rep=rep1&type=pdf)
study showed, is that the errors don't live alone, if you see one, there
likely are already dozens (like bed bugs).
So to tell if the disk is in good or bad condition we just need to find and
confirm _enough_ problematic blocks to say with confidence if the disk
is in good or bad shape.
