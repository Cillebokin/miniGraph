TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += library tests example

library.file = src/MiniGraphLibrary.pro
tests.file = tests/Tests.pro
tests.depends = library
example.file = examples/basic/Basic.pro
example.depends = library

contains(CONFIG, performancebench) {
    SUBDIRS += performancebench
    performancebench.file = benchmarks/PerformanceBenchmarks.pro
    performancebench.depends = library
}
