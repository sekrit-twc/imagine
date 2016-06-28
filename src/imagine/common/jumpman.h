#pragma once

#ifndef IMAGINE_JUMPMAN_H_
#define IMAGINE_JUMPMAN_H_

#include <csetjmp>
#include <exception>
#include <type_traits>
#include <utility>

namespace imagine {

/*************
  ▒▒▒▒▒
  ▒▒▒▒▒▒▒▒▒
  ▓▓▓░░▓░
 ▓░▓░░░▓░░░
 ▓░▓▓░░░▓░░░
 ▓▓░░░░▓▓▓▓
  ░░░░░░░░
  ▓▓▒▓▓▓▒▓▓
 ▓▓▓▒▓▓▓▒▓▓▓
▓▓▓▓▒▒▒▒▒▓▓▓▓
░░▓▒░▒▒▒░▒▓░░
░░░▒▒▒▒▒▒▒░░░
░░▒▒▒▒▒▒▒▒▒░░
  ▒▒▒   ▒▒▒
 ▓▓▓    ▓▓▓
▓▓▓▓    ▓▓▓▓
*************/
class Jumpman {
	template <class T, class...Args>
	struct invoke_function {
		typedef typename std::result_of<T(Args...)>::type result_type;
		typedef typename std::conditional<std::is_void<result_type>::value, char, result_type>::type stack_type;

		template <class R = result_type, typename std::enable_if<std::is_void<R>::value>::type * = nullptr>
		static void invoke(stack_type *, T func, Args &&...args)
		{
			func(std::forward<Args>(args)...);
		}

		template <class R = result_type, typename std::enable_if<!std::is_void<R>::value>::type * = nullptr>
		static void invoke(stack_type *ret, T func, Args &&...args)
		{
			*ret = func(std::forward<Args>(args)...);
		}
	};

	void (*m_default_throw)(void *);
	void *m_default_throw_arg;

	std::exception_ptr m_exception;
	std::jmp_buf m_setjmp;
	bool m_jump_active;

	[[noreturn]] void rethrow_exception();
public:
	Jumpman(void (*default_throw)(void *), void *default_throw_arg) :
		m_default_throw{ default_throw },
		m_default_throw_arg{ default_throw_arg },
		m_jump_active{}
	{
	}

	[[noreturn]] void execute_jump();

	void store_exception();

    template <class T, class ...Args>
    typename std::result_of<T(Args...)>::type call(T func, Args &&...args)
	{
		if (setjmp(m_setjmp)) {
			m_jump_active = false;
			rethrow_exception();
		}

		m_exception = nullptr;
		m_jump_active = true;

		typename invoke_function<T, Args...>::stack_type ret;
		invoke_function<T, Args...>::invoke(&ret, func, std::forward<Args>(args)...);

		m_jump_active = false;

		if (m_exception)
			rethrow_exception();

		return static_cast<typename std::result_of<T(Args...)>::type>(ret);
	}
};

} // namespace imagine

#endif // IMAGINE_JUMPMAN_H_
