#include "stdafx.h"

#include "CNodeResourceImpl.h"
#include "CNodeScriptRuntime.h"

bool CNodeResourceImpl::Start()
{
	v8::Locker locker(isolate);
	v8::Isolate::Scope isolateScope(isolate);
	v8::HandleScope handleScope(isolate);

	v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

	v8::Local<v8::String> resourceName = v8::String::NewFromUtf8(isolate, resource->GetName().CStr(), v8::NewStringType::kNormal).ToLocalChecked();

	global->Set(v8::String::NewFromUtf8(isolate, "__resourceName", v8::NewStringType::kNormal).ToLocalChecked(), resourceName);

	v8::Local<v8::Context> _context = node::NewContext(isolate, global);
	v8::Context::Scope scope(_context);

	_context->SetAlignedPointerInEmbedderData(1, resource);
	context.Reset(isolate, _context);

	V8ResourceImpl::Start();

	nodeData = node::CreateIsolateData(isolate, uv_default_loop(), runtime->GetPlatform());
	const char* argv[] = { "altv-resource" };
	env = node::CreateEnvironment(nodeData, _context, 1, argv, 1, argv, false);

	node::BootstrapEnvironment(env);
	node::LoadEnvironment(env);

	asyncResource.Reset(isolate, v8::Object::New(isolate));
	asyncContext = node::EmitAsyncInit(isolate, asyncResource.Get(isolate), "CNodeResourceImpl");

	while (!envStarted && !startError)
	{
		runtime->OnTick();
		OnTick();
	}

	DispatchStartEvent(startError);

	return !startError;
}

bool CNodeResourceImpl::Stop()
{
	v8::Locker locker(isolate);
	v8::Isolate::Scope isolateScope(isolate);
	v8::HandleScope handleScope(isolate);

	{
		v8::Context::Scope scope(GetContext());
		DispatchStopEvent();

		node::EmitAsyncDestroy(isolate, asyncContext);
		asyncResource.Reset();
	}

	node::EmitBeforeExit(env);
	node::EmitExit(env);
	node::RunAtExit(env);
	
	// TODO: async stop function

	// node::Stop(env);

	node::FreeEnvironment(env);
	node::FreeIsolateData(nodeData);

	return true;
}

void CNodeResourceImpl::Started(v8::Local<v8::Value> _exports)
{
	if (!_exports->IsNullOrUndefined())
	{
		alt::MValueDict exports = V8Helpers::V8ToMValue(_exports).As<alt::IMValueDict>();
		resource->SetExports(exports);
		envStarted = true;
	}
	else
	{
		startError = true;
	}
}

bool CNodeResourceImpl::OnEvent(const alt::CEvent* e)
{
	v8::Locker locker(isolate);
	v8::Isolate::Scope isolateScope(isolate);
	v8::HandleScope handleScope(isolate);

	v8::Context::Scope scope(GetContext());
	//env->PushAsyncCallbackScope();
	
	V8::EventHandler* handler = V8::EventHandler::Get(e);
	if (!handler)
		return true;

	// Generic event handler
	{
		auto evType = e->GetType();
		if(evType == alt::CEvent::Type::CLIENT_SCRIPT_EVENT || evType == alt::CEvent::Type::SERVER_SCRIPT_EVENT)
		{
			std::vector<V8::EventCallback *> callbacks;
			const char* eventName;

			if(evType == alt::CEvent::Type::SERVER_SCRIPT_EVENT) 
			{
				callbacks = std::move(GetLocalHandlers("*"));
				eventName = static_cast<const alt::CServerScriptEvent*>(e)->GetName().CStr();
			}
			else if(evType == alt::CEvent::Type::CLIENT_SCRIPT_EVENT) 
			{
				callbacks = std::move(GetRemoteHandlers("*"));
				eventName = static_cast<const alt::CClientScriptEvent*>(e)->GetName().CStr();
			}

			if(callbacks.size() != 0)
			{
				auto evArgs = handler->GetArgs(this, e);
				evArgs.insert(evArgs.begin(), v8::String::NewFromUtf8(isolate, eventName, v8::NewStringType::kNormal).ToLocalChecked());

				node::CallbackScope callbackScope(isolate, asyncResource.Get(isolate), asyncContext);
				InvokeEventHandlers(e, callbacks, evArgs);
			}
		}
	}

	std::vector<V8::EventCallback*> callbacks = handler->GetCallbacks(this, e);
	if (callbacks.size() > 0)
	{
		std::vector<v8::Local<v8::Value>> args = handler->GetArgs(this, e);

		node::CallbackScope callbackScope(isolate, asyncResource.Get(isolate), asyncContext);
		InvokeEventHandlers(e, callbacks, args);
	}

	//env->PopAsyncCallbackScope();
	return true;
}

void CNodeResourceImpl::OnTick()
{
	v8::Locker locker(isolate);
	v8::Isolate::Scope isolateScope(isolate);
	v8::HandleScope handleScope(isolate);

	v8::Context::Scope scope(GetContext());
	node::CallbackScope callbackScope(isolate, asyncResource.Get(isolate), asyncContext);

	V8ResourceImpl::OnTick();
}
