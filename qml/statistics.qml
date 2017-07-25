
import QtQuick 2.0
import QtCharts 2.0

ChartView {
    theme: ChartView.ChartThemeBrownSand
    antialiasing: true

    StackedBarSeries {
        id: tripDepthSeries
        axisX: BarCategoryAxis { categories: columnsDepthStatistics.names }
        BarSet { label: "min"; values: columnsDepthStatistics.minValues }
        BarSet { label: "max"; values: columnsDepthStatistics.Values }
        BarSet { label: "mean"; values: columnsDepthStatistics.minValues }
    }
}
