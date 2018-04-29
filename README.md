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
depending on HDD size and speed) it will print progress information in form
similar to the one below:

```
hdck status:
============
Loop:          1 of 1
Progress:      2.87%, 2.87% total
Read:          56064000 sectors of 1000204795904
Speed:         121.408MiB/s, average: 94.588MiB/s
Elapsed time:  00:04:49
Expected time: 02:47:50
         Samples:             Blocks:
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

(meaning of particular filelds is explained below)

At the end it will print summary similar to this:

```
results:
possible latent bad sectors or silent realocations:
block 152604 (LBA: 39066624-39066879) rel std dev: 0.092528082, average: 249.413991, valid: yes, samples: 2
block 152605 (LBA: 39066880-39067135) rel std dev: 0.663862467, average: 20.201437, valid: yes, samples: 6
block 365753 (LBA: 93632768-93633023) rel std dev: 0.000327480, average: 18.547844, valid: yes, samples: 2
block 499546 (LBA: 127883776-127884031) rel std dev: 0.667183390, average: 17.377143, valid: yes, samples: 2
block 525720 (LBA: 134584320-134584575) rel std dev: 0.550697986, average: 23.005352, valid: yes, samples: 3
block 1558769 (LBA: 399044864-399045119) rel std dev: 0.668017125, average: 17.565852, valid: yes, samples: 2
block 3375615 (LBA: 864157440-864157695) rel std dev: 0.000375852, average: 16.047311, valid: yes, samples: 5
block 3843711 (LBA: 983990016-983990271) rel std dev: 0.746113563, average: 23.568186, valid: yes, samples: 2
block 4482182 (LBA: 1147438592-1147438847) rel std dev: 0.000182737, average: 34.318936, valid: yes, samples: 2
block 5190989 (LBA: 1328893184-1328893439) rel std dev: 0.661462207, average: 17.735146, valid: yes, samples: 2
block 5308850 (LBA: 1359065600-1359065855) rel std dev: 2.027333942, average: 10.417865, valid: yes, samples: 8
block 5530828 (LBA: 1415891968-1415892223) rel std dev: 0.659776245, average: 17.776946, valid: yes, samples: 2
block 5953060 (LBA: 1523983360-1523983615) rel std dev: 0.000690584, average: 16.329662, valid: yes, samples: 5
block 6404576 (LBA: 1639571456-1639571711) rel std dev: 0.739413966, average: 23.795692, valid: yes, samples: 2
block 7540426 (LBA: 1930349056-1930349311) rel std dev: 0.702445124, average: 16.701053, valid: yes, samples: 2
15 uncertain blocks found

wall time: 15182s.452ms.368µs.313ns
sum time: 11930s.717ms.78µs
tested 7630957 blocks (0 errors, 8411636 samples)
mean block time: 0s.1ms.409µs
std dev: 0.758969303(ms)
Number of invalid blocks because of detected interrupted reads: 0
Number of interrupted reads: 2296
Individual block statistics:
<2.08ms: 6569056
<4.17ms: 1048557
<8.33ms: 1793
<16.67ms: 11539
<33.33ms: 10
<50.00ms: 1
>50.00ms: 1
ERR: 0

Worst blocks:
block no      st.dev  avg   tr. avg max     min    valid  samples
     1558769 11.7343  17.57   17.57   25.86    9.27  yes   2
     5190989 11.7311  17.74   17.74   26.03    9.44  yes   2
     5530828 11.7288  17.78   17.78   26.07    9.48  yes   2
      365753  0.0061  18.55   18.55   18.55   18.54  yes   2
      152605 15.5012  20.20   21.63   34.07    0.64  yes   6
      525720 12.6690  23.01   23.01   34.07    9.18  yes   3
     3843711 17.5845  23.57   23.57   36.00   11.13  yes   2
     6404576 17.5949  23.80   23.80   36.24   11.35  yes   2
     4482182  0.0063  34.32   34.32   34.32   34.31  yes   2
      152604 23.0778 249.41  249.41  265.73  233.10  yes   2


Disk status: CRITICAL
CAUTION! Sectors that required more than 6 read attempts detected, drive may be ALREADY FAILING!
```

As can be read from the status message, this drive is in critical condition.

Disk status can range from:

* **excellent** - self explanatory
* **very good** - When all blocks were read below the rotational latency threshold
* **good** - When there are very few blocks that read constant re-reads (because of bad block reallocations or ECC failures
* **moderate** -  When there are many blocks that need constant re-reads
* **bad** - When blocks that required more than 2 re-reads detected
* **very bad** - when blocks that required more than 4 re-reads were detected
* **critical** - when blocks that required more than 6 read attempts were detected
* **failed** - when disk returned read failures

Basically any disk at very bad or worse requires administrator action (more on
this later), lower levels can be considered as OK.


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
  well-used SSDs welcome)

If the `Interrpt` row grows rapidly, it means that either something is wrong,
usually some background process keeps writing and flushing data, use iotop to
confirm. Sometimes using noatime helps:

```
mount -o remount,noatime /
```

# Advanced usage

TODO
