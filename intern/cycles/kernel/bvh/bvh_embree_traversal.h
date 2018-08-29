/*
 * Copyright 2017 , Blender Foundation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "embree3/rtcore_ray.h"
#include "embree3/rtcore_scene.h"

struct CCLIntersectContext  {
	typedef enum {
		RAY_REGULAR = 0,
		RAY_SHADOW_ALL = 1,
		RAY_SSS = 2,
		RAY_VOLUME_ALL = 3,
		
	} RayType;

	// cycles extensions:
	ccl::KernelGlobals *kg;
	RayType type;

	// for shadow rays
	ccl::Intersection *isect_s;
	int max_hits;
	int num_hits;

	// for SSS Rays:
	ccl::LocalIntersection *ss_isect;
	int sss_object_id;
	ccl::uint *lcg_state;

	CCLIntersectContext(const ccl::Ray& ray, ccl::KernelGlobals *kg_,  RayType type_)
	{
		kg = kg_;
		type = type_;
		max_hits = 1;
		num_hits = 0;
		isect_s = NULL;
		ss_isect = NULL;
		sss_object_id = -1;
		lcg_state = NULL;
	}
};

class IntersectContext
{
public:
	IntersectContext(CCLIntersectContext* ctx)
	{
		rtcInitIntersectContext(&context);
		userRayExt = ctx;
	}
	RTCIntersectContext context;
	CCLIntersectContext* userRayExt;
};

ccl_device_inline void kernel_embree_setup_ray(const ccl::Ray& ray, RTCRay& rtc_ray, const ccl::uint visibility)
{
	rtc_ray.org_x = ray.P.x;
	rtc_ray.org_y = ray.P.y;
	rtc_ray.org_z = ray.P.z;
	rtc_ray.dir_x = ray.D.x;
	rtc_ray.dir_y = ray.D.y;
	rtc_ray.dir_z = ray.D.z;
	rtc_ray.tnear = 0.0f;
	rtc_ray.tfar = ray.t;
	rtc_ray.time = ray.time;
	rtc_ray.mask = visibility;
}

ccl_device_inline void kernel_embree_setup_rayhit(const ccl::Ray& ray, RTCRayHit& rayhit, const ccl::uint visibility)
{
	kernel_embree_setup_ray(ray, rayhit.ray, visibility);
	rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
	rayhit.hit.primID = RTC_INVALID_GEOMETRY_ID;
}

ccl_device_inline void kernel_embree_convert_hit(ccl::KernelGlobals *kg, const RTCRay *ray, const RTCHit *hit, ccl::Intersection *isect)
{
	bool is_hair = hit->geomID & 1;
	isect->u = is_hair ? hit->u : 1.0f - hit->v - hit->u;
	isect->v = is_hair ? hit->v : hit->u;
	isect->t = ray->tfar;
	isect->Ng = ccl::make_float3(hit->Ng_x, hit->Ng_y, hit->Ng_z);
	if(hit->instID[0] != RTC_INVALID_GEOMETRY_ID) {
		RTCScene inst_scene = (RTCScene)rtcGetGeometryUserData(rtcGetGeometry(kernel_data.bvh.scene, hit->instID[0]));
		isect->prim = hit->primID + (intptr_t)rtcGetGeometryUserData(rtcGetGeometry(inst_scene, hit->geomID)) + kernel_tex_fetch(__object_node, hit->instID[0]/2);
		isect->object = hit->instID[0]/2;
	}
	else {
		isect->prim = hit->primID + (intptr_t)rtcGetGeometryUserData(rtcGetGeometry(kernel_data.bvh.scene, hit->geomID));
		isect->object = OBJECT_NONE;
	}
	isect->type = kernel_tex_fetch(__prim_type, isect->prim);
}
