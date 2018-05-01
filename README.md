# hdck - Hard Drive Check

Testing condition of HDD, SSD, CD, DVD, etc. media.

This tool allows for checking if the storage device is healthy (does not have
any badblocks or latent bad sectors).
Allowing for non-destructive and online assessment of HDD or SSD condition.

# Build

To compile the program, download the latest package from release page or
git repo and execute:

```
make
```

If the compilation succeeds, you'll get one executable file: `hdck`. You can
either copy it to `/sbin` or just run it from local directory. The rest of this
manual will assume the former option.

# Basic usage

To test the the only disk in the PC, run the program as this:

```
hdck -f /dev/sda
```

While the program is running (it can take from half an hour, to 12h or more,
depending on HDD's size and speed) it will print progress information in form
similar to the one below:

```
hdck status:
============
Loop:          1 of 1
Progress:      1.83%, 1.83% total
Read:          768000 sectors of 21474836480
Speed:         185.433MiB/s, average: 172.749MiB/s
Elapsed time:  00:00:02
Expected time: 00:01:49
         Samples:             Blocks (9th decile):
< 2.4ms:                 2961                 2961
< 5.8ms:                    7                    7
<14.1ms:                   19                   19
<22.5ms:                    7                    7
<39.1ms:                    3                    3
<55.8ms:                    2                    2
>55.8ms:                    1                    1
ERR    :                    0                    0
Intrrpt:                    0                    0
```

(meaning of particular fields is explained below)

At the end it will print summary similar to this:

```
hdck results:
=============
possible latent bad sectors or silent realocations:
block 0 (LBA: 0-255) rel std dev:  -nan, avg:  2.50, valid: yes, samples: 1, errors: 0, 9th decile:  2.50
block 1 (LBA: 256-511) rel std dev:  -nan, avg:  2.55, valid: yes, samples: 1, errors: 0, 9th decile:  2.55
block 2616 (LBA: 669696-669951) rel std dev:  0.00, avg: 37.37, valid: yes, samples: 15, errors: 0, 9th decile: 37.42
block 2643 (LBA: 676608-676863) rel std dev:  0.00, avg: 35.90, valid: yes, samples: 15, errors: 0, 9th decile: 35.93
block 2657 (LBA: 680192-680447) rel std dev:  0.00, avg: 35.89, valid: yes, samples: 15, errors: 0, 9th decile: 35.94
block 15177 (LBA: 3885312-3885567) rel std dev:  0.90, avg: 11.39, valid: yes, samples: 20, errors: 0, 9th decile: 22.66
block 33574 (LBA: 8594944-8595199) rel std dev:  0.59, avg: 14.31, valid: yes, samples: 15, errors: 0, 9th decile: 29.31
block 33862 (LBA: 8668672-8668927) rel std dev:  0.87, avg:  7.92, valid: yes, samples: 14, errors: 0, 9th decile: 19.20
block 33981 (LBA: 8699136-8699391) rel std dev:  0.01, avg:  6.43, valid: yes, samples: 14, errors: 0, 9th decile: 15.74
block 34139 (LBA: 8739584-8739839) rel std dev:  0.81, avg: 20.48, valid: yes, samples: 20, errors: 0, 9th decile: 45.94
block 34166 (LBA: 8746496-8746751) rel std dev:  0.82, avg:  7.81, valid: yes, samples: 19, errors: 0, 9th decile: 20.96
block 34192 (LBA: 8753152-8753407) rel std dev:  0.85, avg: 11.33, valid: yes, samples: 19, errors: 0, 9th decile: 22.67
block 34368 (LBA: 8798208-8798463) rel std dev:  0.67, avg: 22.02, valid: yes, samples: 24, errors: 0, 9th decile: 55.14
block 34814 (LBA: 8912384-8912639) rel std dev:  0.79, avg: 10.29, valid: yes, samples: 24, errors: 0, 9th decile: 26.55
block 34837 (LBA: 8918272-8918527) rel std dev:  0.67, avg: 18.53, valid: yes, samples: 24, errors: 0, 9th decile: 37.66
block 34891 (LBA: 8932096-8932351) rel std dev:  0.50, avg: 42.55, valid: yes, samples: 30, errors: 0, 9th decile: 62.59
block 37690 (LBA: 9648640-9648895) rel std dev:  0.89, avg: 11.96, valid: yes, samples: 20, errors: 0, 9th decile: 22.42
block 37987 (LBA: 9724672-9724927) rel std dev:  0.53, avg: 23.39, valid: yes, samples: 20, errors: 0, 9th decile: 45.02
block 39149 (LBA: 10022144-10022399) rel std dev:  0.25, avg: 30.48, valid: yes, samples: 20, errors: 0, 9th decile: 45.06
block 41191 (LBA: 10544896-10545151) rel std dev:  0.30, avg: 143.75, valid: yes, samples: 30, errors: 0, 9th decile: 229.82
block 43859 (LBA: 11227904-11228159) rel std dev:  0.55, avg: 27.37, valid: yes, samples: 30, errors: 0, 9th decile: 63.21
block 45148 (LBA: 11557888-11558143) rel std dev:  0.26, avg: 100.32, valid: yes, samples: 30, errors: 0, 9th decile: 145.01
block 47789 (LBA: 12233984-12234239) rel std dev:  0.67, avg: 22.79, valid: yes, samples: 20, errors: 0, 9th decile: 40.72
block 74821 (LBA: 19154176-19154431) rel std dev:  0.24, avg: 71.71, valid: yes, samples: 30, errors: 0, 9th decile: 120.03
block 80023 (LBA: 20485888-20486143) rel std dev:  0.72, avg: 33.94, valid: yes, samples: 30, errors: 0, 9th decile: 85.84
block 81849 (LBA: 20953344-20953599) rel std dev:  0.53, avg: 15.89, valid: yes, samples: 15, errors: 0, 9th decile: 32.56
block 90502 (LBA: 23168512-23168767) rel std dev:  0.48, avg: 48.78, valid: yes, samples: 30, errors: 0, 9th decile: 89.06
block 91273 (LBA: 23365888-23366143) rel std dev:  0.86, avg: 10.55, valid: yes, samples: 20, errors: 0, 9th decile: 20.99
block 91507 (LBA: 23425792-23426047) rel std dev:  0.43, avg: 28.06, valid: yes, samples: 20, errors: 0, 9th decile: 46.87
block 96648 (LBA: 24741888-24742143) rel std dev:  0.53, avg: 22.24, valid: yes, samples: 20, errors: 0, 9th decile: 38.52
block 98009 (LBA: 25090304-25090559) rel std dev:  0.38, avg: 68.93, valid: yes, samples: 30, errors: 0, 9th decile: 112.58
block 98190 (LBA: 25136640-25136895) rel std dev:  0.32, avg: 63.20, valid: yes, samples: 30, errors: 0, 9th decile: 105.19
block 104310 (LBA: 26703360-26703615) rel std dev:  0.42, avg: 47.65, valid: yes, samples: 30, errors: 0, 9th decile: 89.85
block 191916 (LBA: 49130496-49130751) rel std dev:  0.40, avg: 19.45, valid: yes, samples: 20, errors: 0, 9th decile: 37.76
block 273929 (LBA: 70125824-70126079) rel std dev:  0.45, avg: 35.09, valid: yes, samples: 30, errors: 0, 9th decile: 65.39
block 301249 (LBA: 77119744-77119999) rel std dev:  0.84, avg: 24.71, valid: yes, samples: 21, errors: 0, 9th decile: 54.10
block 301250 (LBA: 77120000-77120255) rel std dev:  0.40, avg: 10.40, valid: yes, samples: 20, errors: 0, 9th decile: 14.17
block 301272 (LBA: 77125632-77125887) rel std dev:  0.91, avg: 17.24, valid: yes, samples: 20, errors: 0, 9th decile: 39.82
block 304234 (LBA: 77883904-77884159) rel std dev:  0.00, avg: 37.69, valid: yes, samples: 15, errors: 0, 9th decile: 37.71
39 uncertain blocks found

wall time: 3481s.825ms.341µs.155ns
sum time: 3435s.169ms.124µs
tested 305333 blocks (0 errors, 964536 samples)
mean block time: 0s.3ms.555µs
std dev: 1.058807835(ms)
Number of invalid blocks because of detected interrupted reads: 0
Number of interrupted reads: 29
Individual block statistics:
<2.44ms: 0
<5.79ms: 299889
<14.12ms: 5383
<22.46ms: 30
<39.12ms: 12
<55.79ms: 8
>55.79ms: 11
ERR: 0

Worst blocks:
block no      st.dev  avg   1stQ    med     3rdQ   valid samples 9th decile
       43859 20.5902  27.37   12.37   20.66   35.28  yes  30     63.21
      273929 22.9114  35.09   13.70   38.67   47.01  yes  30     65.39
       80023 30.7677  33.94   10.89   23.40   50.46  yes  30     85.84
       90502 30.5331  48.78   20.73   41.54   70.73  yes  30     89.06
      104310 33.9484  47.65   29.02   37.38   54.06  yes  30     89.85
       98190 37.7779  63.20   39.70   58.49   85.47  yes  30    105.19
       98009 37.8684  68.93   35.89   77.53   94.24  yes  30    112.58
       74821 35.9626  71.71   60.81   60.89   90.06  yes  30    120.03
       45148 35.2132 100.32   77.49   85.93  127.55  yes  30    145.01
       41191 61.4519 143.75   97.81  133.18  179.01  yes  30    229.82

Disk status: CRITICAL
CAUTION! Sectors that required more than 6 read attempts detected, drive may be ALREADY FAILING!
```

As can be read from the status message, this drive is in critical condition.

Disk status can range from:

* **excellent** - self explanatory
* **very good** - When all blocks were read below the rotational latency threshold
* **good** - When there are very few blocks that read constant re-reads (because of bad block reallocations or ECC failures)
* **moderate** -  When there are many blocks that need constant re-reads
* **bad** - When blocks that required more than 2 re-reads detected
* **very bad** - when blocks that required more than 4 re-reads were detected
* **critical** - when blocks that required more than 6 read attempts were detected
* **failed** - when disk returned read failures

Basically any disk at very bad or worse requires administrator action (more on
this later), lower levels can be considered as OK.

Note: the very fact that HDDs are physical systems, makes this scan
not fully deterministic, but if there were multiple sectors that required
multiple re-reads from disk (time larger than 16.66ms for 7200 rpm disk)
during _any_ one scan it means that the disk may be beginning to fail.
See more in the
[THEORY_OF_OPERATION.md](https://github.com/tomato42/hdck/blob/master/THEORY_OF_OPERATION.md)
document.

# Nomenclature

## scan status

When the scan is running, the status look similar to this:

```
hdck status:
============
Loop:          1 of 1
Progress:      2.87%, 2.87% total
Read:          56064000 sectors of 1000204795904
Speed:         121.408MiB/s, average: 94.588MiB/s
Elapsed time:  00:04:49
Expected time: 02:47:50
         Samples:             Blocks (9th decile):
< 2.1ms:               190880               190788
< 4.2ms:                26886                26877
< 8.3ms:                   13                   13
<16.7ms:                  661                  642
<33.3ms:                  130                  113
<50.0ms:                  201                   28
>50.0ms:                   26                  175
ERR    :                    0                    0
Intrrpt:                  203                  364
```

* `Loop` - the current and programmed total number of whole-disk reads that the
  application will perform
* `Progress` - what percentage of the current read has been performed and how
  much is that in light of the total number of expected reads
* `Read` - current sector number (LBA) being read and the total number of
  sectors on the device
* `Speed` - current and average speed of reading the device
* `Elapsed time` - how long has the `hdck` been running, in hours, minutes and
  seconds
* `Expected time` - what is the expected total runtime of the application, in
  hours, minutes and seconds
* `Samples` - total number of blocks with a given property
* `Blocks` - total number of blocks for which the calculated (or estimated)
  [ninth decile](https://en.wikipedia.org/wiki/Quantile) places them in a
  given category
* `2.1ms`, `4.2ms`, etc. - the amount of time it at most took the hard drive to
  read the sector (the exact numbers depend on rotational speed of the
  hard drive, the numbers are calculated for groupings of normal reads,
  reads with cylinder change, 1 re-read, 2 re-reads, 4 re-reads, 6 re-reads
  and more than 6 re-reads)
* `ERR` - sectors for which an IO error was returned (the read failed
  completely)
* `Intrrpt` - reads which were interrupted - when `hdck` detected that other
  process was accessing the drive, they will be ignored and sectors will
  be re-read

## Scan report
The most important part of the scan is the list of the worst blocks:

```
Worst blocks:
block no      st.dev  avg   1stQ    med     3rdQ   valid samples 9th decile
        7390  6.6420   4.84    1.01    1.01    6.76  yes   3     10.21
        6821  5.2559   6.81    4.80    8.75    9.78  yes   3     10.40
       19616  6.9332   4.99    0.99    0.99    7.00  yes   3     10.60
       18108  6.9555   4.98    0.96    0.98    7.00  yes   3     10.60
       25216  5.9841   6.12    2.67    2.67    7.85  yes   3     10.96
       29680  5.2407   5.08    0.97    0.97    9.56  yes  12     12.66
        1912  5.3093   4.41    0.98    1.02    7.49  yes  15     12.92
       30218  8.2052   6.03    2.11    2.64    6.56  yes   4     13.58
        2471  6.0064   7.13    2.70    2.71   14.69  yes  11     14.70
       24895 11.3644   7.34    0.78    0.98   10.72  yes   3     16.57
```
(note, the list is sorted in reverse order - worst last - to ensure that the
most important info is shown even on 80x25 rescue terminals)

Here, the explanation for columns is as follows:

* `block no` - block number (multiply by 256 to get LBA of first sector)
* `st.dev` - standard deviation of all the samples for the block
* `avg` - average (arithmetic mean) of oll the samples
* `1stQ` - first quartile - no more than 25% of the block reads took this much
  time (in ms)
* `med` - median - no more than 50% of the block reads took this much time (in
  ms)
* `3rdQ` - third quartile - no more than 75% of reads took this much time (in
  ms)
* `valid` - is the statistic valid; for sectors near bad blocks (IO errors)
  the collection may be not possible
* `samples` - number of times the block was read
* `9th decile` - no more than 90% of reads took this much time to complete (in
  ms)


# Use notes
## What not to do

As with all IT systems, this program is no exception to the "garbage in,
garbage out" rule. To get meaningful test results, the test environment must be
relatively calm.

Things to avoid:

* running downloads (or p2p systems like torrents)
* applications that intensively use the hdd, use `iotop` or `atop` to find them
* applications that frequently write to hdd, The usual culprits on desktop
  computers are `akonadi`, `nepomuk` and `nscd`. All of them can be turned off.
* running tests on disk connected to computer using non native interface,
  especially USB or Firewire enclosures, some IDE-SATA bridges can degrade the
  meaningfulness of data. Use `--ata-verify` to workaround this.
* Running in PIO mode or degraded DMA, all IDE disks produced since 2005 years
  should work with UDMA 100 or UDMA 133 (check your dmsg). Similarly SATA 3.0
  disk should not be run on SATA 1.0 interface. Basically, don't run fast disk
  on limiting interface. If the disk is able to read at 60MiB/s (that's normal
  for 120G 7200rpm HDDs) then UDMA-66 or any PIO mode will be too slow, so will
  be USB-2.0 or FireWire-400, older SCSI such as Ultra2, Ultra or Ultra Wide
  SCSI will limit the drive too. Use `--ata-verify` to workaround this.
* Testing SSDs is supported but possibly meaningless, the only thing this
  application will detect correctly are hard read errors, and if SSD is in
  such state it's long gone. (people willing to collect statistics of
  well-used SSDs welcome - see [#2](https://github.com/tomato42/hdck/issues/2))

If the `Interrpt` row grows rapidly, it means that either something is wrong,
usually some background process keeps writing and flushing data, use iotop to
confirm. Sometimes using noatime helps:

```
mount -o remount,noatime /
```

# Advanced usage

TODO

# Thanks

* Dmitry Postrigan for MHDD, the main source of inspiration for `hdck`
