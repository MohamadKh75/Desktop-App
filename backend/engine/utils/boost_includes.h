#ifndef BOOST_INCLUDES_H
#define BOOST_INCLUDES_H

#if defined __cplusplus

#include <boost/asio.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/array.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>

typedef boost::shared_ptr<boost::asio::ip::tcp::socket> socket_ptr;

#endif

#endif // BOOST_INCLUDES_H
