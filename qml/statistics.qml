
import QtQuick 2.0
import QtCharts 2.0

ChartView {
    antialiasing: true

    BarSeries {
        id: tripDepthSeries
        labelsPosition: AbstractBarSeries.LabelsInsideBase;

        axisY: ValueAxis { min:columnsDepthStatistics.min; max: columnsDepthStatistics.max;}
        axisX: BarCategoryAxis { categories: columnsDepthStatistics.names }


        BarSet { label: "min"; values: columnsDepthStatistics.minValues }
        BarSet { label: "mean"; values: columnsDepthStatistics.meanValues }
        BarSet { label: "max"; values: columnsDepthStatistics.maxValues }
    }
}
