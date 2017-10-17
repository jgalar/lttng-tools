#!/bin/bash

TRACE=$1

tar cvzf ${TRACE}.tar.gz $TRACE
rm -rf $TRACE
