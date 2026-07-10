// to create a Python bridge
#include <pybind11/pybind11.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "orderbook.hpp"

//defined in main.cpp
void run_kalshi_feed(std::shared_ptr<SharedOrderBook> book, std::string market_ticker, bool print_updates);

namespace py = pybind11;

class PyKalshiBook{
    public:
        explicit PyKalshiBook(std::string market_ticker) : book_(std::make_shared<SharedOrderBook>()){
            std::thread(run_kalshi_feed, book_, std::move(market_ticker), false);
        }

        BookTop get_best_bid_ask() const {
            return book_->read_snapshot();
        }
    private:
        std::shared_ptr<SharedOrderBook> book_;
};

PYBIND11_MODULE(kalshi_orderbook, m){
    m.doc() = "Live kalshi order book, updated on a background C++ thread";

    // register the BookTop struct so Python can understand it
    py::class_<BookTop>(m, "BookTop")
        .def_readonly("yes_bid", &BookTop::yes_bid)
        .def_readonly("yes_ask", &BookTop::yes_ask)
        .def_readonly("no_bid", &BookTop::no_bid)
        .def_readonly("no_ask", &BookTop::no_ask)
        .def_readonly("is_synced", &BookTop::is_synced);
    
    //register OrderBook Class
    py::class_<PyKalshiBook>(m, "OrderBook")    
        .def(py::init<std::string>(), py::arg("market_ticker"))
        .def("get_best_bid_ask", &PyKalshiBook::get_best_bid_ask,
            py::call_guard<py::gil_scoped_release>(),
            "Returns (yes_bid, yes_ask) in price ticks, -1 means no resting orders")
}