#!/bin/sh

XMP_PATH=$1
if test -f $XMP_PATH; then
    echo "make XMP_PATH=[install path]"
    exit 1
fi
SORTED_LIST=$2
BASE_TESTDIR=$3

for subdir in `awk '{print $2}' $SORTED_LIST`; do
    subdir=${subdir%/}
    BFILE=${BASE_TESTDIR}/$subdir/slurm.sh
    NORM_DIRNAME=`echo ${BASE_TESTDIR}/$subdir | sed "s/\//\\\\\\\\\//g"`
    sed "s/@DIRNAME@/$NORM_DIRNAME/" ./tests/xcalablemp/scripts/slurm_template.sh > $BFILE
    JOBNAME=`echo $subdir | awk -F/ '{print $(NF-1)"-"$NF}'`
    sed "s/@JOBNAME@/$JOBNAME/" $BFILE > $BFILE-2
    NORM_XMP_PATH=`echo $XMP_PATH | sed "s/\//\\\\\\\\\//g"`
    sed "s/@XMP_PATH@/$NORM_XMP_PATH/" $BFILE-2 > $BFILE; rm $BFILE-2
    sbatch $BFILE
done
   