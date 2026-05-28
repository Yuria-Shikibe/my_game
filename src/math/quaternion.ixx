module;

#include <cassert>

export module mo_yanxi.math.quaternion;

import std;
import mo_yanxi.math;
import mo_yanxi.math.vector3;
import mo_yanxi.math.matrix4;
import mo_yanxi.tags;

namespace mo_yanxi::math{
export struct quaternion{
	using value_type = float;
	using vec3_t = vector3<float>;
	using mat4_t = matrix4;

private:
	value_type x_{0.f};
	value_type y_{0.f};
	value_type z_{0.f};
	value_type w_{1.f};

	[[nodiscard]] static constexpr bool needs_flip(const value_type x, const value_type y, const value_type z,
	                                               const value_type w) noexcept{
		return w < 0.f || (w == 0.f && (x < 0.f || (x == 0.f && (y < 0.f || (y == 0.f && z < 0.f)))));
	}

	[[nodiscard]] static constexpr quaternion raw(const value_type x, const value_type y, const value_type z,
	                                              const value_type w) noexcept{
		return quaternion{tags::unchecked, x, y, z, w};
	}

	[[nodiscard]] static constexpr vec3_t normalize_vec3(const vec3_t v) noexcept{
		const auto len2 = v.length2();
		if(len2 == 0.f) [[unlikely]] return {};
		const auto inv_len = 1.f / mo_yanxi::math::sqrt(len2);
		return {v.x * inv_len, v.y * inv_len, v.z * inv_len};
	}

	[[nodiscard]] static constexpr quaternion normalized(const value_type x, const value_type y, const value_type z,
	                                                     const value_type w) noexcept{
		const auto len2 = x * x + y * y + z * z + w * w;
		if(len2 == 0.f) [[unlikely]] return quaternion{};

		const auto inv_len = 1.f / mo_yanxi::math::sqrt(len2);
		value_type nx = x * inv_len;
		value_type ny = y * inv_len;
		value_type nz = z * inv_len;
		value_type nw = w * inv_len;

		if(needs_flip(nx, ny, nz, nw)){
			nx = -nx;
			ny = -ny;
			nz = -nz;
			nw = -nw;
		}

		return raw(nx, ny, nz, nw);
	}

	[[nodiscard]] static constexpr quaternion from_matrix3x3(
		const value_type m00, const value_type m01, const value_type m02,
		const value_type m10, const value_type m11, const value_type m12,
		const value_type m20, const value_type m21, const value_type m22
	) noexcept{
		const auto trace = m00 + m11 + m22;

		if(trace > 0.f){
			const auto s = 2.f * mo_yanxi::math::sqrt(trace + 1.f);
			return normalized(
				(m21 - m12) / s,
				(m02 - m20) / s,
				(m10 - m01) / s,
				0.25f * s
			);
		}

		if(m00 > m11 && m00 > m22){
			const auto s = 2.f * mo_yanxi::math::sqrt(1.f + m00 - m11 - m22);
			return normalized(
				0.25f * s,
				(m01 + m10) / s,
				(m02 + m20) / s,
				(m21 - m12) / s
			);
		}

		if(m11 > m22){
			const auto s = 2.f * mo_yanxi::math::sqrt(1.f + m11 - m00 - m22);
			return normalized(
				(m01 + m10) / s,
				0.25f * s,
				(m12 + m21) / s,
				(m02 - m20) / s
			);
		}

		const auto s = 2.f * mo_yanxi::math::sqrt(1.f + m22 - m00 - m11);
		return normalized(
			(m02 + m20) / s,
			(m12 + m21) / s,
			0.25f * s,
			(m10 - m01) / s
		);
	}

	[[nodiscard]] constexpr value_type scalar_dot(const quaternion& rhs) const noexcept{
		return x_ * rhs.x_ + y_ * rhs.y_ + z_ * rhs.z_ + w_ * rhs.w_;
	}

public:
	constexpr quaternion() noexcept = default;

	[[nodiscard]] constexpr explicit quaternion(
		tags::unchecked_t,
		const value_type x,
		const value_type y,
		const value_type z,
		const value_type w) noexcept
		: x_{x}, y_{y}, z_{z}, w_{w}{
		assert(math::equal(x * x + y * y + z * z + w * w, 1.f, 0.1f));
	}

	[[nodiscard]] constexpr explicit quaternion(
		const value_type x,
		const value_type y,
		const value_type z,
		const value_type w) noexcept
		: quaternion(normalized(x, y, z, w)){
	}

	[[nodiscard]] static constexpr quaternion identity() noexcept{
		return {};
	}

	[[nodiscard]] constexpr value_type x() const noexcept{ return x_; }
	[[nodiscard]] constexpr value_type y() const noexcept{ return y_; }
	[[nodiscard]] constexpr value_type z() const noexcept{ return z_; }
	[[nodiscard]] constexpr value_type w() const noexcept{ return w_; }

	[[nodiscard]] constexpr vec3_t xyz() const noexcept{
		return {x_, y_, z_};
	}

	[[nodiscard]] constexpr value_type length2() const noexcept{
		return x_ * x_ + y_ * y_ + z_ * z_ + w_ * w_;
	}

	[[nodiscard]] constexpr bool is_normalized(const value_type margin = 0.000001f) const noexcept{
		return mo_yanxi::math::abs(length2() - 1.f) <= margin;
	}

	[[nodiscard]] constexpr quaternion normalized_copy() const noexcept{
		return normalized(x_, y_, z_, w_);
	}

	constexpr quaternion& normalize() noexcept{
		return *this = normalized_copy();
	}

	[[nodiscard]] static constexpr quaternion from_axis_angle(vec3_t axis, const value_type radians) noexcept{
		axis = normalize_vec3(axis);
		if(axis.length2() == 0.f) [[unlikely]] return identity();

		const auto half = radians * 0.5f;
		const auto s = mo_yanxi::math::sin(half);
		const auto c = mo_yanxi::math::cos(half);
		return normalized(axis.x * s, axis.y * s, axis.z * s, c);
	}

	[[nodiscard]] static constexpr quaternion from_axis_angle_deg(vec3_t axis, const value_type degrees) noexcept{
		return from_axis_angle(axis, degrees * mo_yanxi::math::deg_to_rad_v<value_type>);
	}

	[[nodiscard]] static constexpr quaternion from_euler(
		const value_type yaw,
		const value_type pitch,
		const value_type roll
	) noexcept{
		return from_axis_angle({0.f, 1.f, 0.f}, yaw)
			* from_axis_angle({1.f, 0.f, 0.f}, pitch)
			* from_axis_angle({0.f, 0.f, 1.f}, roll);
	}

	[[nodiscard]] static constexpr quaternion from_matrix(const mat4_t& m) noexcept{
		const vec3_t c0{m.c0.r, m.c0.g, m.c0.b};
		const vec3_t c1{m.c1.r, m.c1.g, m.c1.b};
		const vec3_t c2{m.c2.r, m.c2.g, m.c2.b};

		const auto n0 = normalize_vec3(c0);
		const auto n1 = normalize_vec3(c1);
		const auto n2 = normalize_vec3(c2);

		return from_matrix3x3(
			n0.x, n1.x, n2.x,
			n0.y, n1.y, n2.y,
			n0.z, n1.z, n2.z
		);
	}

	[[nodiscard]] static constexpr quaternion from_to(vec3_t from, vec3_t to) noexcept{
		from = normalize_vec3(from);
		to = normalize_vec3(to);

		if(from.length2() == 0.f || to.length2() == 0.f) [[unlikely]] return identity();

		const auto dot = from.dot(to);
		if(dot >= 1.f) [[unlikely]] return identity();

		if(dot <= -0.999999f){
			vec3_t axis = mo_yanxi::math::X3.cross(from);
			if(axis.length2() == 0.f) axis = mo_yanxi::math::Y3.cross(from);
			return from_axis_angle(axis, std::numbers::pi_v<value_type>);
		}

		const auto axis = from.cross(to);
		return normalized(axis.x, axis.y, axis.z, 1.f + dot);
	}

	[[nodiscard]] constexpr quaternion conjugate() const noexcept{
		return normalized(-x_, -y_, -z_, w_);
	}

	[[nodiscard]] constexpr quaternion inverse() const noexcept{
		return conjugate();
	}

	[[nodiscard]] constexpr quaternion operator-() const noexcept{
		return normalized(-x_, -y_, -z_, -w_);
	}

	constexpr quaternion& operator*=(const quaternion& rhs) noexcept{
		return *this = *this * rhs;
	}

	[[nodiscard]] friend constexpr quaternion operator*(const quaternion& lhs, const quaternion& rhs) noexcept{
		return normalized(
			lhs.w_ * rhs.x_ + lhs.x_ * rhs.w_ + lhs.y_ * rhs.z_ - lhs.z_ * rhs.y_,
			lhs.w_ * rhs.y_ - lhs.x_ * rhs.z_ + lhs.y_ * rhs.w_ + lhs.z_ * rhs.x_,
			lhs.w_ * rhs.z_ + lhs.x_ * rhs.y_ - lhs.y_ * rhs.x_ + lhs.z_ * rhs.w_,
			lhs.w_ * rhs.w_ - lhs.x_ * rhs.x_ - lhs.y_ * rhs.y_ - lhs.z_ * rhs.z_
		);
	}

	[[nodiscard]] constexpr value_type dot(const quaternion& rhs) const noexcept{
		return scalar_dot(rhs);
	}

	[[nodiscard]] constexpr vec3_t rotate(const vec3_t v) const noexcept{
		const vec3_t qv{x_, y_, z_};
		const vec3_t t = qv.cross(v) * 2.f;
		return v + t * w_ + qv.cross(t);
	}

	[[nodiscard]] constexpr mat4_t to_matrix() const noexcept{
		const auto xx = x_ * x_;
		const auto yy = y_ * y_;
		const auto zz = z_ * z_;
		const auto xy = x_ * y_;
		const auto xz = x_ * z_;
		const auto yz = y_ * z_;
		const auto wx = w_ * x_;
		const auto wy = w_ * y_;
		const auto wz = w_ * z_;

		return mat4_t{
				{1.f - 2.f * (yy + zz), 2.f * (xy + wz), 2.f * (xz - wy), 0.f},
				{2.f * (xy - wz), 1.f - 2.f * (xx + zz), 2.f * (yz + wx), 0.f},
				{2.f * (xz + wy), 2.f * (yz - wx), 1.f - 2.f * (xx + yy), 0.f},
				{0.f, 0.f, 0.f, 1.f},
			};
	}

	[[nodiscard]] constexpr explicit operator mat4_t() const noexcept{
		return to_matrix();
	}

	[[nodiscard]] constexpr std::pair<vec3_t, value_type> to_axis_angle() const noexcept{
		const auto v_len = mo_yanxi::math::sqrt(x_ * x_ + y_ * y_ + z_ * z_);
		if(v_len == 0.f) [[unlikely]] return {mo_yanxi::math::X3, 0.f};
		return {{x_ / v_len, y_ / v_len, z_ / v_len}, 2.f * mo_yanxi::math::atan2(v_len, w_)};
	}

	[[nodiscard]] static constexpr quaternion slerp(const quaternion& a, const quaternion& b,
	                                                const value_type t) noexcept{
		quaternion end = b;
		const auto dot = a.dot(end);
		if(dot < 0.f) end = -end;

		const auto cos_theta = mo_yanxi::math::abs(a.dot(end));
		const auto sin_theta = mo_yanxi::math::sqrt(mo_yanxi::math::max(0.f, 1.f - cos_theta * cos_theta));

		if(sin_theta == 0.f) [[unlikely]]{
			return normalized(
				a.x_ + (end.x_ - a.x_) * t,
				a.y_ + (end.y_ - a.y_) * t,
				a.z_ + (end.z_ - a.z_) * t,
				a.w_ + (end.w_ - a.w_) * t
			);
		}

		const auto theta = mo_yanxi::math::atan2(sin_theta, cos_theta);
		const auto w0 = mo_yanxi::math::sin((1.f - t) * theta) / sin_theta;
		const auto w1 = mo_yanxi::math::sin(t * theta) / sin_theta;

		return normalized(
			a.x_ * w0 + end.x_ * w1,
			a.y_ * w0 + end.y_ * w1,
			a.z_ * w0 + end.z_ * w1,
			a.w_ * w0 + end.w_ * w1
		);
	}

	[[nodiscard]] constexpr bool operator==(const quaternion& rhs) const noexcept = default;
};

export using quat = quaternion;
}
