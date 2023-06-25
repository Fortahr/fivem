using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Security;

#if IS_FXSERVER
using ContextType = CitizenFX.Core.fxScriptContext;
#else
using ContextType = CitizenFX.Core.RageScriptContext;
#endif

/*
* Notes while working on this environment:
*  - Scheduling: any function that can potentially add tasks to the C#'s scheduler needs to return the time
*    of when it needs to be activated again, which then needs to be scheduled in the core scheduler (bookmark).
*/

namespace CitizenFX.Core
{
	[SecurityCritical, SuppressMessage("System.Diagnostics.CodeAnalysis", "IDE0051", Justification = "Called by host")]
	internal static class ScriptInterface
	{
		private static UIntPtr s_runtime;
		internal static int InstanceId { get; private set; }
		internal static string ResourceName { get; private set; }
		internal static CString CResourceName { get; private set; }

		#region Callable from C#

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern void CFree(IntPtr ptr);

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern void Print(string channel, string text);

		/*[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern ulong GetMemoryUsage();

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern bool SnapshotStackBoundary(out byte[] data);

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern bool WalkStackBoundary(string resourceName, byte[] start, byte[] end, out byte[] blob);*/

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern bool ProfilerIsRecording();

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern void ProfilerEnterScope(string name);

		[ MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern void ProfilerExitScope();

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern UIntPtr GetNative(ulong hash);

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern unsafe bool InvokeNative(UIntPtr native, ref ContextType context, ulong hash);

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern unsafe byte[] CanonicalizeRef(UIntPtr host, int refId);

		internal static unsafe byte[] CanonicalizeRef(int refId) => CanonicalizeRef(s_runtime, refId);

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern unsafe void RegisterExport(UIntPtr runtime, string exportName, ulong privateId, ulong binding);
		
		internal static unsafe void RegisterExport(string exportName, ulong privateId, ulong binding) => RegisterExport(s_runtime, exportName, privateId, binding);

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern unsafe byte InvokeExternalExport(UIntPtr runtime, string resourceName, string exportName, byte[] argumentData, out byte* result, out ulong resultSize, out ulong asyncRedultId);

		internal static unsafe byte InvokeExternalExport(string resourceName, string exportName, byte[] argumentData, out byte* result, out ulong resultSize, out ulong asyncRedultId)
			=> InvokeExternalExport(s_runtime, resourceName, exportName, argumentData, out result, out resultSize, out asyncRedultId);

		[MethodImpl(MethodImplOptions.InternalCall)]
		internal static extern unsafe byte OutgoingAsyncResult(ulong asyncRedultId, byte status, byte* result, ulong resultSize);

		[MethodImpl(MethodImplOptions.InternalCall)]
		private static extern unsafe bool ReadAssembly(UIntPtr host, string file, out byte[] assembly, out byte[] symbols);

		internal static unsafe bool ReadAssembly(string file, out byte[] assembly, out byte[] symbols) => ReadAssembly(s_runtime, file, out assembly, out symbols);

		#endregion

		#region Called by Native
		internal static void Initialize(string resourceName, UIntPtr runtime, int instanceId)
		{
			s_runtime = runtime;
			InstanceId = instanceId;
			ResourceName = resourceName;
			CResourceName = resourceName;

			Resource.Current = new Resource(resourceName);
			Debug.Initialize(resourceName);
			Scheduler.Initialize();

			ExportsManager.Initialize(resourceName);
#if REMOTE_FUNCTION_ENABLED
			ExternalsManager.Initialize(resourceName, instanceId);
#endif
		}

		internal static ulong Tick(ulong hostTime, bool profiling)
		{
			Scheduler.CurrentTime = (TimePoint)hostTime;
			Profiler.IsProfiling = profiling;

			try
			{
				ScriptContext.CleanUp();
				Scheduler.Update();
			}
			catch (Exception e)
			{
				Debug.PrintError(e, "Tick()");
			}

			return Scheduler.NextTaskTime();
		}

		internal static unsafe ulong TriggerEvent(string eventName, byte* argsSerialized, int serializedSize, string sourceString, ulong hostTime, bool profiling)
		{
			Scheduler.CurrentTime = (TimePoint)hostTime;
			Profiler.IsProfiling = profiling;
			
			Binding origin = !sourceString.StartsWith("net") ? Binding.Local : Binding.Remote;

#if IS_FXSERVER
			sourceString = (origin == Binding.Remote || sourceString.StartsWith("internal-net")) ? sourceString : null;
#else
			sourceString = origin == Binding.Remote ? sourceString : null;
#endif

			object[] args = null; // will make sure we only deserialize it once
#if REMOTE_FUNCTION_ENABLED
			if (!ExternalsManager.IncomingRequest(eventName, sourceString, origin, argsSerialized, serializedSize, ref args))
#endif
			{
				if (!ExportsManager.IncomingRequest(eventName, sourceString, origin, argsSerialized, serializedSize, ref args))
				{
					// if a remote function or export has consumed this event then it surely wasn't meant for event handlers
					EventsManager.IncomingEvent(eventName, sourceString, origin, argsSerialized, serializedSize, args);
				}
			}
			
			return Scheduler.NextTaskTime();
		}

		internal static unsafe ulong LoadAssembly(string name, ulong hostTime, bool profiling)
		{
			Scheduler.CurrentTime = (TimePoint)hostTime;
			Profiler.IsProfiling = profiling;

			ScriptManager.LoadAssembly(name, true);

			return Scheduler.NextTaskTime();
		}

		internal static unsafe ulong CallRef(int refIndex, byte* argsSerialized, uint argsSize, out IntPtr retvalSerialized, out uint retvalSize, ulong hostTime, bool profiling)
		{
			Scheduler.CurrentTime = (TimePoint)hostTime;
			Profiler.IsProfiling = profiling;

			ReferenceFunctionManager.IncomingCall(refIndex, argsSerialized, argsSize, out retvalSerialized, out retvalSize);

			return Scheduler.NextTaskTime();
		}

		internal static int DuplicateRef(int refIndex)
		{
			return ReferenceFunctionManager.IncrementReference(refIndex);
		}

		internal static void RemoveRef(int refIndex)
		{
			ReferenceFunctionManager.DecrementReference(refIndex);
		}

		internal static unsafe ulong InvokeExport(ulong privateId, byte* arguments, ulong argumentsSize, out byte status, ref byte* result, ref ulong resultSize, ulong asyncResultId, ulong hostTime, bool profiling)
		{
			Scheduler.CurrentTime = (TimePoint)hostTime;
			Profiler.IsProfiling = profiling;

			status = (byte)ExternalsManager.IncomingExport(privateId, arguments, argumentsSize, ref result, ref resultSize, asyncResultId);
			return Scheduler.NextTaskTime();
		}

		internal static unsafe ulong AsyncResult(ulong asyncResultId, ExternalsManager.StatusCode status, byte* arguments, ulong argumentsSize, ulong hostTime, bool profiling)
		{
			Scheduler.CurrentTime = (TimePoint)hostTime;
			Profiler.IsProfiling = profiling;

			ExternalsManager.IncomingAsyncResult(asyncResultId, status, arguments, argumentsSize);
			return Scheduler.NextTaskTime();
		}

		#endregion
	}

	[SecuritySafeCritical]
	public class RemoveMeTest
	{
		public static void RegisterExport(string exportName, DynFunc dynFunc, Binding binding)
			=> ExternalsManager.RegisterExport(exportName, dynFunc, binding);

		public static unsafe Coroutine<dynamic> InvokeExternalExport(string resourceName, string exportName, byte[] argumentData)
			=> ExternalsManager.InvokeExternalExport(resourceName, exportName, argumentData);
	}
}
