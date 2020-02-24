#!/bin/bash
#
# Copyright (C) 2020 Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
#
# SPDX-License-Identifier: MIT

plugin_path=$(dirname "$0")

DATA1="[\"duration\", \"D1\", \"performance:hit\", [\"source\", \"iteration\"], \"performance:receive\", [\"source\", \"iteration\"]]"

while read -r load delay count; do
	S=$(echo "scale=3; 1 / ( $delay / 1000 )" | bc | awk '{printf "%.3f", $0}' );
	hz=${S/.000/}
	hz_title=${hz/./-}

	echo "Graphing Hz: ${hz} CPU: ${load}"

	PLOT1="[\"Trigger latency, Freq:${hz}Hz, CPU load: ${load}%\", \"T (ms)\", \"count\", [$DATA1]]"

	babeltrace2 --plugin-path="$plugin_path" --component sink.plot.PlotSink \
		--params="histograms=[$PLOT1]"	\
		"./trace/${load}_cpuload_${delay}ms"
done << EOF
0 1 10000
25 1 10000
50 1 10000
75 1 10000
100 1 10000
0 10 5000
25 10 5000
50 10 5000
75 10 5000
100 10 5000
0 100 6000
25 100 6000
50 100 6000
75 100 6000
100 100 6000
0 1000 1500
25 1000 1500
50 1000 1500
75 1000 1500
100 1000 1500
0 10000 300
25 10000 300
50 10000 300
75 10000 300
100 10000 300
0 60000 50
25 60000 50
50 60000 50
75 60000 50
100 60000 50
EOF

pdf_unite=""
csvs=""
# Generate united graph and base pdf list to unite
while read -r delay ; do
	S=$(echo "scale=3; 1 / ( $delay / 1000 )" | bc | awk '{printf "%.3f", $0}' );
	hz=${S/.000/}
	hz_title=${hz/./-}
	local_pdf_unite=""
	echo "Combining graphs for Hz: ${hz} hz_title: $hz_title"

	loggers=""
	for load in 100 75 50 25 0; do
		path=trigger-latency-freq-${hz_title}hz-cpu-load-${load}-.D1.csv
		csvs="$csvs $path"
		loggers="[\"duration-csv\", \"${load}% CPU\", \"${path}\"], $loggers"
		local_pdf_unite="${local_pdf_unite} trigger-latency-freq-${hz_title}hz-cpu-load-${load}-.pdf"
	done
	pdf_unite="$pdf_unite $local_pdf_unite"

	PLOT1="[\"Trigger latency, Freq:${hz}Hz\", \"T (ms)\", \"count\", [$loggers]]"
	babeltrace2 --plugin-path="$plugin_path" --component sink.plot.PlotSink \
		--params="histograms=[$PLOT1]"	\
		"./trace/0_cpuload_${delay}ms"
done << EOF
1
10
100
1000
10000
60000
EOF

# Add united graphs to the pdfunite cmd
while read -r delay ; do
	S=$(echo "scale=3; 1 / ( $delay / 1000 )" | bc | awk '{printf "%.3f", $0}' );
	hz=${S/.000/}
	hz_title=${hz/./-}

	pdf_unite="trigger-latency-freq-${hz_title}hz.pdf $pdf_unite"

done << EOF
60000
10000
1000
100
10
1
EOF

rm -rf $csvs

pdfunite $pdf_unite summary.pdf

