using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security;

namespace CitizenFX.Core
{
	static class ExternalsManager
	{
		public enum StatusCode : byte
		{
			SUCCESSFUL,
			ASYNC,
			REMOTE,

			FUNCTION_INACCASSIBLE,
			FUNCTION_NOT_FOUND,
			RESOURCE_NOT_FOUND,
			FAILED,
		};

		private static readonly Dictionary<ulong, DynFunc> s_exports = new Dictionary<ulong, DynFunc>();
		private static readonly Dictionary<ulong, Coroutine<dynamic>> s_asyncs = new Dictionary<ulong, Coroutine<dynamic>>();
		private static readonly List<GCHandle> s_results = new List<GCHandle>();

		internal static unsafe StatusCode IncomingExport(ulong privateId, byte* arguments, ulong argumentsSize, ref byte* resultData, ref ulong resultSize, ulong asyncResultId)
		{
			for (int i = 0; i < s_results.Count; ++i)
				s_results[i].Free();

			s_results.Clear();

			if (s_exports.TryGetValue(privateId, out var dynFunc))
			{
#if IS_FXSERVER
				Remote remote = new Remote((asyncResultId & 0xFFFF).ToString());
#else
				Remote remote = new Remote((asyncResultId & 0xFFFF) != 0);
#endif
				object[] args = MsgPackDeserializer.DeserializeArray(arguments, (long)argumentsSize);
				var result = dynFunc(remote, args);

				if (result is Coroutine coroutine)
				{
					coroutine.ContinueWith(c => OutgoingAsyncResult(c, asyncResultId));
					return StatusCode.ASYNC;
				}
				else
				{
					byte[] resultSerialized = MsgPackSerializer.Serialize(new object[] { result });
					var gcHandle = GCHandle.Alloc(resultSerialized, GCHandleType.Pinned);
					s_results.Add(gcHandle);

					resultData = (byte*)gcHandle.AddrOfPinnedObject();
					resultSize = (ulong)resultSerialized.LongLength;

					return StatusCode.SUCCESSFUL;
				}
			}

			return StatusCode.FUNCTION_NOT_FOUND;
		}

		[SecuritySafeCritical]
		public static unsafe void RegisterExport(string exportName, DynFunc dynFunc, Binding binding)
		{
			ulong privateId = (ulong)dynFunc.GetHashCode();
			if (dynFunc.Target != null)
				privateId ^= (ulong)dynFunc.Target.GetHashCode();

			// keep incrementing until we find a free spot
			while (s_exports.ContainsKey(privateId)) unchecked { ++privateId; }
			s_exports.Add(privateId, dynFunc);

			ScriptInterface.RegisterExport(exportName, privateId, (ulong)binding);
		}

		[SecuritySafeCritical]
		public static unsafe Coroutine<dynamic> InvokeExternalExport(string resourceName, string exportName, byte[] argumentData)
		{
			var status = (StatusCode)ScriptInterface.InvokeExternalExport(resourceName, exportName, argumentData, out var resultData, out ulong resultSize, out ulong asyncResultId);

			switch (status)
			{
				case StatusCode.SUCCESSFUL:
					if (resultData != null)
					{
						object[] objects = MsgPackDeserializer.DeserializeArray(resultData, (long)resultSize);
						if (objects.Length > 0)
							return Coroutine.Completed<dynamic>(objects[0]);
					}

					return Coroutine.Completed<dynamic>(null);

				case StatusCode.ASYNC:
				case StatusCode.REMOTE:
					return s_asyncs[asyncResultId] = new Coroutine<dynamic>();

				case StatusCode.FUNCTION_INACCASSIBLE:
					throw new ExternalAccessException($"{resourceName}:{exportName}");

				case StatusCode.FUNCTION_NOT_FOUND:
				case StatusCode.RESOURCE_NOT_FOUND:
					throw new ExternalMissingException($"{resourceName}:{exportName}");

				case StatusCode.FAILED:
					throw new ExternalFailedException($"{resourceName}:{exportName}");

				default:
					throw new NotSupportedException("This shouldn't happen, please report with a repo.");
			}
		}

		[SecuritySafeCritical]
		public static unsafe void OutgoingAsyncResult(Coroutine coroutine, ulong asyncResultId)
		{
			var result = coroutine.GetResult();

			byte[] resultSerialized = MsgPackSerializer.Serialize(new object[] { result });
			var gcHandle = GCHandle.Alloc(resultSerialized, GCHandleType.Pinned);
			s_results.Add(gcHandle);

			var resultData = (byte*)gcHandle.AddrOfPinnedObject();
			var resultSize = (ulong)resultSerialized.LongLength;

			ScriptInterface.OutgoingAsyncResult(asyncResultId, (byte)StatusCode.SUCCESSFUL, resultData, resultSize);
		}

		[SecuritySafeCritical]
		public static unsafe void IncomingAsyncResult(ulong asyncResultId, StatusCode status, byte* arguments, ulong argumentsSize)
		{
			if (s_asyncs.TryGetValue(asyncResultId, out var coroutine))
			{
				s_asyncs.Remove(asyncResultId);

				try
				{

					switch (status)
					{
						case StatusCode.SUCCESSFUL:
							object[] objects = MsgPackDeserializer.DeserializeArray(arguments, (long)argumentsSize);
							coroutine.Complete(objects.Length > 0 ? objects[0] : null);
							break;
						case StatusCode.FUNCTION_INACCASSIBLE:
							coroutine.Fail(null, new ExternalAccessException());
							break;
						case StatusCode.FUNCTION_NOT_FOUND:
						case StatusCode.RESOURCE_NOT_FOUND:
							coroutine.Fail(null, new ExternalMissingException());
							break;
						case StatusCode.FAILED:
							coroutine.Fail(null, new ExternalFailedException());
							break;
						default:
							coroutine.Fail(null, new NotSupportedException("This shouldn't happen, please report with a repo."));
							break;
					}
				}
				catch (Exception ex)
				{
					Debug.WriteLine(ex);
				}
			}
		}
	}

	public class ExternalMissingException : Exception
	{
		public ExternalMissingException() : base() { }
		public ExternalMissingException(string message) : base(message) { }
	}

	public class ExternalAccessException : Exception
	{
		public ExternalAccessException() : base() { }
		public ExternalAccessException(string message) : base(message) { }
	}

	public class ExternalFailedException : Exception
	{
		public ExternalFailedException() : base() { }
		public ExternalFailedException(string message) : base(message) { }
	}
}
