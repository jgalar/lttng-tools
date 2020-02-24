import bt2
import itertools
import matplotlib.pyplot as plt
import sys
import statistics
import csv
from collections import defaultdict


class DataLogger(object):
    def __init__(self, name="Untitled"):
        self._name = name

    def get_name(self):
        return self._name

    def get_x_data(self):
        raise NotImplementedError

    def get_y_data(self):
        raise NotImplementedError

    def received_event(self, ts, event):
        raise NotImplementedError


class DurationDataLogger(DataLogger):
    """
        This class allow to create a duration histogram for the given pair of
        event and unique tuple key generator.

    """
    def __init__(self, start_event, end_event, *args, **kwargs):
        super(DurationDataLogger, self).__init__(*args, **kwargs)

        (self._event_start, self._start_fields) = start_event
        (self._event_end, self._end_fields) = end_event

        self._durations = []
        self._pair = dict()

    def get_x_data(self):
        return self._durations

    def received_event(self, ts, event):
        if event.name == self._event_start:
            key = ()
            for field in self._start_fields:
                value = event.payload_field[str(field)]
                key = key + (value,)
            self._pair[key] = ts
            return

        if event.name == self._event_end:
            key = ()
            for field in self._end_fields:
                value = event.payload_field[str(field)]
                key = key + (value,)

            if key not in self._pair:
                print("unmatched end event")
                return

            start_ts = self._pair[key]
            duration = (ts - start_ts) / 1000000.0
            self._durations.append(duration)

class DurationCSVDataLogger(DataLogger):
    """
        This class allow to create a duration histogram for the given csv.
    """
    def __init__(self, filepath, *args, **kwargs):
        super(DurationCSVDataLogger, self).__init__(*args, **kwargs)

        self._filepath = filepath


        self._durations = []
        with open(filepath, newline='') as file:
            reader = csv.reader(file, quoting=csv.QUOTE_NONE)
            next(reader)
            for row in reader:
                self._durations.append(float(row[0]))

    def get_x_data(self):
        return self._durations

    def received_event(self, ts, event):
        return


class Plot(object):
    def __init__(
        self, loggers, title="Untitled", x_label="Untitled", y_label="Untitled"
    ):
        self._loggers = loggers
        self._title = title
        self._x_label = x_label
        self._y_label = y_label

    def received_event(self, ts, event):
        for logger in self._loggers:
            logger.received_event(ts, event)

    def plot(self):
        raise NotImplementedError

    def generate_csv(self):
        raise NotImplementedError

    @staticmethod
    def _format_filename(title, ext):
        title = title.lower()
        title = "".join("-" if not c.isalnum() else c for c in title)
        title = "".join(
            ["".join(j) if i != "-" else i for (i, j) in itertools.groupby(title)]
        )
        return f"{title}.{ext}"

class HistogramPlot(Plot):
    def __init__(self, *args, **kwargs):
        super(HistogramPlot, self).__init__(*args, **kwargs)

    @staticmethod
    def get_statistics_header():
        return ["minimum", "maximum", "mean", "pstdev", "count"]

    @staticmethod
    def get_statistics(samples):
        stats = []
        stats.append('%f' % min(samples))
        stats.append('%f' % max(samples))
        stats.append('%f' % statistics.mean(samples))
        stats.append('%f' % statistics.pstdev(samples))
        stats.append('%d' % len(samples))
        return stats

    def plot(self):
        sys.argv = ['']
        complete_set = [];
        logger_statistic = defaultdict(dict)

        figure = plt.figure()
        plt.title(self._title)
        plt.xlabel(self._x_label, figure=figure)
        plt.ylabel(self._y_label, figure=figure)
        plt.yscale('log', nonposy='clip')

        table_rows_label = []
        table_celltext = []
        for logger in self._loggers:
            x = logger.get_x_data()
            table_rows_label.append(logger.get_name())
            table_celltext.append(HistogramPlot.get_statistics(x))

            complete_set +=x;
            plt.hist(x, bins='auto', alpha=0.5, figure=figure, label=logger.get_name())

        table_rows_label.append("all")
        table_celltext.append(HistogramPlot.get_statistics(complete_set))
        the_table = plt.table(cellText=table_celltext,
                rowLabels=table_rows_label,
                colLabels=HistogramPlot.get_statistics_header(),
                loc='bottom',
                bbox=[0.0,-0.45,1,.28],
                )

        the_table.auto_set_font_size(False)
        the_table.set_fontsize(8)

        plt.subplots_adjust(bottom=0.20)
        plt.legend(loc='center left', bbox_to_anchor=(1, 0.5))
        plt.savefig(Plot._format_filename(self._title, "pdf"), bbox_inches="tight")

    def generate_csv(self):
        for logger in self._loggers:
            x_data = logger.get_x_data()
            with open(Plot._format_filename(self._title, "%s.csv" % logger.get_name()), 'w', newline='') as export:
                wr = csv.writer(export, quoting=csv.QUOTE_NONE)
                wr.writerow([self._x_label])
                for x in x_data:
                    wr.writerow([x])


@bt2.plugin_component_class
class PlotSink(bt2._UserSinkComponent):
    def __init__(self, config, params, obj):
        self._plots = []

        if "histograms" in params:
            for plot in params["histograms"]:
                self._plots.append(PlotSink.create_histogram(plot))

        self._add_input_port("in")

    def _user_consume(self):
        msg = next(self._iter)
        if type(msg) in [
            bt2._PacketBeginningMessageConst,
            bt2._PacketEndMessageConst,
            bt2._StreamBeginningMessageConst,
            bt2._StreamEndMessageConst,
        ]:
            return

        ts = msg.default_clock_snapshot.value
        for plot in self._plots:
            plot.received_event(ts, msg.event)

    def _user_finalize(self):
            {plot.plot() for plot in self._plots}
            {plot.generate_csv () for plot in self._plots}
            return

    def _user_graph_is_configured(self):
        self._iter = self._create_message_iterator(self._input_ports["in"])

    @staticmethod
    def create_histogram(params):
        loggers = []
        for logger in params[3]:
            if logger[0] == "duration":
                logger = PlotSink.create_duration_logger(logger)
            elif logger[0] == "duration-csv":
                logger = PlotSink.create_duration_logger_csv(logger)
            else:
                raise ValueError

            loggers.append(logger)

        title = str(params[0])
        x_label = str(params[1])
        y_label = str(params[2])

        return HistogramPlot(loggers, title=title, x_label=x_label,
                y_label=y_label)

    @staticmethod
    def create_duration_logger(params):
        return DurationDataLogger(
            (str(params[2]), params[3]),
            (str(params[4]), params[5]),
            name=str(params[1]),
        )

    def create_duration_logger_csv(params):
        return DurationCSVDataLogger(
            str(params[2]),
            name=str(params[1]),
        )


bt2.register_plugin(
    module_name=__name__,
    name="plot",
    description="Plot Sink",
    author="EfficiOS inc.",
    license="GPL",
    version=(1, 0, 0),
)
