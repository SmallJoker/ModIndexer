using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Collections.Generic;
using HtmlAgilityPack;
using Newtonsoft.Json;

namespace ModIndexer
{
	class Program
	{
		static void Main(string[] args)
		{
			Console.Title = "Minetest Mod Indexer - Let me index the stuff for you.";
			Console.Write("Select a mode:\n" +
					"\n\t1) Released mods  [full]" +
					"\n\t2) WIP mods       [full]" +
					"\n\t3) Released games [full]" +
					"\n\t4) WIP games      [full]" +
					"\n\t5) Client mods    [full]" +
					"\n\t6) Old mods       [fast]" +
					"\n\t7) Exit" +
					"\n\nYour choice: ");

			ConsoleKeyInfo k = new ConsoleKeyInfo();
			while (k.Key < ConsoleKey.D1 ||
					k.Key > ConsoleKey.D7) {

				k = Console.ReadKey(true);

				if (k.Key == ConsoleKey.D7)
					return;
			}
			Console.WriteLine(k.KeyChar + "\n");

			Misc.FETCH_TYPE forum;

			switch (k.Key) {
			case ConsoleKey.D1:
				forum = Misc.FETCH_TYPE.REL_MODS;
				break;
			case ConsoleKey.D2:
				forum = Misc.FETCH_TYPE.WIP_MODS;
				break;
			case ConsoleKey.D3:
				forum = Misc.FETCH_TYPE.REL_GAMES;
				break;
			case ConsoleKey.D4:
				forum = Misc.FETCH_TYPE.WIP_GAMES;
				break;
			case ConsoleKey.D5:
				forum = Misc.FETCH_TYPE.CSM_MODS;
				break;
			case ConsoleKey.D6:
				forum = Misc.FETCH_TYPE.OLD_MODS;
				break;
			default:
				Console.WriteLine("Error: Keys are not properly implemented!");
				return;
			}

			Console.Write("Start page: ");
			string start = Console.ReadLine();
			Console.Write("End page: ");
			string stop = Console.ReadLine();

			new Engine(forum, start, stop);

			Console.WriteLine("=== DONE!\nPress any key to exit.");
			Console.ReadKey(false);
		}
	}

	class Engine
	{
		System.Text.Encoding enc = System.Text.Encoding.UTF8;
		List<ForumData> update_data;
		WebClient cli = new WebClient();
		Misc.FETCH_TYPE forum;

		public Engine(Misc.FETCH_TYPE forum, string start, string stop)
		{
			this.forum = forum;

			update_data = new List<ForumData>();
			ServicePointManager.ServerCertificateValidationCallback += ValidateRemoteCertificate;
			int begin, end;
			if (!int.TryParse(start, out begin))
				begin = 1;

			if (!int.TryParse(stop, out end))
				end = 1;

			for (int i = begin - 1; i < end; i++)
				FetchTopicList(i);

			byte[] answer = Config.Upload(ref update_data);

			try {
				int[] topics = JsonConvert.DeserializeObject<int[]>(enc.GetString(answer));

				foreach (ForumData d in update_data) {
					for (int i = 0; i < topics.Length; i++) {
						if (d.topicId == topics[i]) {
							Console.WriteLine((d.type == 0 ? "RM\t" : "\t") + d.title);
							break;
						}
					}
				}
			} catch (Exception e) {
				Console.WriteLine(e.ToString());
				//Console.WriteLine(enc.GetString(answer));
			}

			Console.WriteLine("Done.");
		}

		bool ValidateRemoteCertificate(object sender,
			System.Security.Cryptography.X509Certificates.X509Certificate cert,
			System.Security.Cryptography.X509Certificates.X509Chain chain,
			System.Net.Security.SslPolicyErrors error)
		{
			// If the certificate is a valid, signed certificate, return true.
			if (error == System.Net.Security.SslPolicyErrors.None ||
				error == System.Net.Security.SslPolicyErrors.RemoteCertificateNameMismatch)
				return true;

			Console.WriteLine("X509Certificate [{0}] Policy Error: '{1}'",
				cert.Subject,
				error.ToString());

			return true;
		}

		// Download page and convert to a HtmlNode object
		HtmlNodeCollection OpenPage(string url, string nodes)
		{
			string text = enc.GetString(cli.DownloadData(url));

			HtmlDocument htmlDoc = new HtmlAgilityPack.HtmlDocument();
			htmlDoc.LoadHtml(text);

			if (htmlDoc.ParseErrors != null) {
				int count = 0;
				foreach (HtmlParseError er in htmlDoc.ParseErrors) {
					count++;
				}
				if (count > 0) {
					Console.WriteLine("ParseErrors: " + count);
				}
			}

			// If valid html page
			if (htmlDoc.DocumentNode == null) {
				Console.WriteLine("DocumentNode null");
				return null;
			}
			HtmlNodeCollection bodyNode = htmlDoc.DocumentNode.SelectNodes(nodes);

			if (bodyNode == null) {
				Console.WriteLine("bodyNode null");
				return null;
			}
			return bodyNode;
		}

		// Analyze the topic list in a forum
		void FetchTopicList(int page)
		{
			Console.WriteLine("=== Forum {0} ({1}) - Page {2}",
				forum.ToString(), (int)forum, page + 1);

			HtmlNodeCollection bodyNode = OpenPage(
				"https://forum.minetest.net/viewforum.php?f=" + (int)forum + "&start=" + (page * 30),
				"//ul[@class='topiclist topics']//dt/a");

			if (bodyNode == null)
				return;

			bool get_author = false;
			string title = "",
				author = "";
			int topicId = 0,
				authorId = 0;

			foreach (HtmlNode dtNode in bodyNode) {
				#region extract IDs
				string link = dtNode.GetAttributeValue("href", "");
				link = link.Replace("&amp;", "&");
				string[] linkArgs = link.Split('&');

				if (!get_author) {
					for (int i = 0; i < linkArgs.Length; i++) {
						if (linkArgs[i].Length < 3)
							continue;

						string first = linkArgs[i][0].ToString() + linkArgs[i][1];
						if (first == "t=") {
							topicId = int.Parse(linkArgs[i].Remove(0, 2));
						}
					}
					title = dtNode.InnerText;
				} else {
					for (int i = 0; i < linkArgs.Length; i++) {
						if (linkArgs[i].Length < 3)
							continue;

						string first = linkArgs[i][0].ToString() + linkArgs[i][1];
						if (first == "u=") {
							authorId = int.Parse(linkArgs[i].Remove(0, 2));
						}
					}
					author = dtNode.InnerText;
				}
				#endregion
				if (!get_author || title.Length < 10)
					goto flip;

				#region filter
				Misc.DATA_TYPE type;
				string mod_name;
				title = parseTitle(title, out mod_name, out type);

				switch (type) {
				case Misc.DATA_TYPE.REL_MOD:
					switch (forum) {
					case Misc.FETCH_TYPE.REL_MODS:
						// Ok.
						break;
					case Misc.FETCH_TYPE.WIP_MODS:
						type = Misc.DATA_TYPE.WIP_MOD;
						break;
					case Misc.FETCH_TYPE.OLD_MODS:
						type = Misc.DATA_TYPE.OLD_MOD;
						break;
					default:
						Console.WriteLine("INFO: Found a mod in the wrong place");
						break;
					}
					break;
				case Misc.DATA_TYPE.REL_MP:
					switch (forum) {
					case Misc.FETCH_TYPE.REL_MODS:
						// Ok.
						break;
					case Misc.FETCH_TYPE.WIP_MODS:
						type = Misc.DATA_TYPE.WIP_MP;
						break;
					case Misc.FETCH_TYPE.OLD_MODS:
						type = Misc.DATA_TYPE.OLD_MOD;
						break;
					default:
						Console.WriteLine("INFO: Found a modpack in the wrong place");
						break;
					}
					break;
				case Misc.DATA_TYPE.REL_GAME:
					switch (forum) {
					case Misc.FETCH_TYPE.REL_GAMES:
						// Ok.
						break;
					case Misc.FETCH_TYPE.WIP_GAMES:
					case Misc.FETCH_TYPE.WIP_MODS:
						type = Misc.DATA_TYPE.WIP_GAME;
						break;
					//case Misc.FETCH_TYPE.OLD_GAMES: // TODO
					case Misc.FETCH_TYPE.OLD_MODS:
						type = Misc.DATA_TYPE.OLD_MOD;   
						break;
					default:
						Console.WriteLine("INFO: Found a subgame in the wrong place");
						break;
					}
					break;
				case Misc.DATA_TYPE.REL_CSM:
					switch (forum) {
					case Misc.FETCH_TYPE.CSM_MODS:
						// Ok.
						break;
					case Misc.FETCH_TYPE.WIP_MODS:
						type = Misc.DATA_TYPE.WIP_CSM;
						break;
					case Misc.FETCH_TYPE.OLD_MODS:
						type = Misc.DATA_TYPE.OLD_MOD;
						break;
					default:
						Console.WriteLine("INFO: Found a CSM in the wrong place");
						break;
					}
					break;
				}
				#endregion

				ForumData info = new ForumData(
					topicId,
					title.EscapeXML(),
					(int)type,
					authorId,
					author,
					"" // Yet unknown
				);

				if (type != Misc.DATA_TYPE.INVALID && forum != Misc.FETCH_TYPE.OLD_MODS) {
					// Fetch topics, get download/source links
					FetchSingleTopic(mod_name, ref info);

					// TODO: Find an use for is_git
				}

				/*Console.WriteLine("Found mod: " + mod_name + 
					"\n\tTag: " + mod_tag +
					"\n\tLink: " + download +
					"\n\tType: " + (int)type + " " + type.ToString());*/

				update_data.Add(info);

				// Empty for next fetch
				author = "";
				title = "";


			flip:
				get_author ^= true;
			}
		}

		const int PRIORITY_WORST = 0xFFFF;

		// Analyze topic contents and get link
		void FetchSingleTopic(string mod_name, ref ForumData info)
		{
			Console.WriteLine("=== Topic " + info.topicId);
			Thread.Sleep(200);

			HtmlNodeCollection bodyNode =
				OpenPage(
					"https://forum.minetest.net/viewtopic.php?t=" + info.topicId,
					"//div[@class='content']"
				);

			if (bodyNode == null)
				return;

			HtmlNodeCollection content = bodyNode[0].SelectNodes(".//a[@class='postlink']");

			if (content == null) {
				Console.WriteLine("\tNo download links embedded.");
				// Topic is dead. Remove mod.
				info.type = (int)Misc.DATA_TYPE.INVALID;
				return;
			}

			string link = "";
			int uglyness = PRIORITY_WORST;

			foreach (HtmlNode dtNode in content) {
				string url_raw = dtNode.GetAttributeValue("href", "");
				string text = dtNode.InnerText;

				if (url_raw[url_raw.Length - 1] == '/')
					url_raw = url_raw.Remove(url_raw.Length - 1);

				string url_new;
				int priority = checkLinkPattern(url_raw, out url_new);

				string lower = url_new.ToLower().Replace('-', '_');
				if (lower.Contains(mod_name))
					uglyness -= 3;
				if (lower.Contains(info.userName.ToLower()))
					uglyness--;

				if (priority < uglyness) {
					if (isLinkAvailable(ref url_new)) {
						// Best link so far. Take it.
						link = url_new;
						uglyness = priority;
					}
				}
			}

			// Can't be worse than empty
			info.link = link;
		}

		int checkLinkPattern(string url_raw, out string url_new)
		{
			const string github = "|/archive/*"; // also for notabug.org
			const string gitlab = "|/repository/*";
			const string bitbucket = "|/get/*|/downloads/*";

			// Sort by priority for link quality, where 0 = highest
			string[] patterns = {
				// Similar formatted git
				@"^(https?:/(/[\w_.-]*){3})(/?$|\.git$" + github + gitlab + bitbucket + ")",
				// repo.or.cz
				@"^(https?://repo\.or\.cz/[\w_.-]*\.git)(/?$|/snapshot/*)",
				// Forum absolute link
				@"^(https?://forum\.minetest\.net/download/file.php\?id=\d*)$"
			};

			// Convert attachment links to proper ones
			if (url_raw.StartsWith("./download/file.php?id="))
				url_raw = url_raw.Replace(".", "https://forum.minetest.net");

			for (int p = 0; p < patterns.Length; p++) {
				var reg1 = new System.Text.RegularExpressions.Regex(patterns[p]);
				var match = reg1.Match(url_raw);

				if (match.Value == "")
					continue;

				// This one matches
				url_new = match.Groups[1].ToString();
				return p * 10;
			}
			url_new = url_raw;
			return PRIORITY_WORST; // None matches
		}

		bool isLinkAvailable(ref string url)
		{
			// We need TLS 1.2 for GitHub but Mono doesn't support that yet
			// Use curl instead and hope the servers support HEAD requests

			var proc_info = new System.Diagnostics.ProcessStartInfo();
			proc_info.FileName = "curl";
			proc_info.Arguments = "-L -I " + url;
			proc_info.UseShellExecute = false;
			proc_info.RedirectStandardOutput = true;
			proc_info.RedirectStandardError = true;
			var curl = System.Diagnostics.Process.Start(proc_info);
			Thread.Sleep(100);

			bool was_empty = true;
			int status = 404;

			while (!curl.StandardOutput.EndOfStream) {
				string line = curl.StandardOutput.ReadLine();
				// Get the line after the last blank one
				if (line == "") {
					was_empty = true;
					continue;
				}

				if (!was_empty) {
					// Look out for "Location: " and get the correct URL
					if (line.StartsWith("Location: "))
						url = line.Substring(10);
					continue;
				}

				was_empty = false;
				string[] parts = line.Split(' ');
				if (parts.Length < 3)
					continue; // This should not happen

				int.TryParse(parts[1], out status);
			}

			return status == 200;
		}

		// Remove useless tags from the forum titles
		const string MODNAME_ALLOWED_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789_-";
		// Content of [tags]
		string[] bad_content = { "wip", "beta", "test", "code", "indev", "git", "github" };
		// Beginnings of [mod-my_doors5] for wrong formatted titles
		string[] bad_prefix = { "minetest", "mod", "mods" };

		string parseTitle(string title, out string mod_name, out Misc.DATA_TYPE mod_tag)
		{
			mod_tag = Misc.DATA_TYPE.INVALID;
			mod_name = "";

			string raw = title;
			int pos = 0,
				open_pos = 0;
			bool opened = false,
				delete = false;

			for (int i = 0; pos < raw.Length; i++, pos++) {
				char cur = title[i];

				if (cur == '[' || cur == '{') {
					opened = true;
					open_pos = pos;
					continue;
				}
				if (cur == ']' || cur == '}') {
					// Tag closed
					opened = false;

					int len = pos - open_pos + 1;
					string content = raw.Substring(open_pos + 1, len - 2).ToLower().Trim();
					double num = 0.0f;
					bool is_number = double.TryParse(content, out num);
					Misc.DATA_TYPE tag = Misc.getDataType(content);

					if (!is_number && mod_tag == Misc.DATA_TYPE.INVALID
							&& tag != Misc.DATA_TYPE.INVALID) {
						// Mod tag detected
						mod_tag = tag;
						delete = true;
					}

					if (delete || is_number
							|| bad_content.IndexOf(content) != -1
							|| tag != Misc.DATA_TYPE.INVALID) {

						// Remove this tag
						raw = raw.Remove(open_pos, len);
						pos -= len;
						delete = false;
						continue;
					}

					content = content.Replace('-', '_');
					int start_substr = 0;

					foreach (string prefix in bad_prefix) {
						if (content.Length <= start_substr + prefix.Length + 1)
							break;

						if (content.Substring(start_substr, prefix.Length) == prefix)
							start_substr += prefix.Length;

						if (content[start_substr] == '_')
							start_substr++;
					}

					if (start_substr == 0) {
						// Everything fine, nothing to replace
						mod_name = content;
					} else {
						// Replace this tag with the proper name
						mod_name = content.Substring(start_substr);
						raw = raw.Remove(open_pos + 1, start_substr);
						pos -= start_substr;
					}

					delete = false;
				}
				if (opened && MODNAME_ALLOWED_CHARS.IndexOf(cur) == -1) {
					delete = true;
				}
			}

			delete = true;
			pos = 0;

			// Trim double whitespaces
			char[] ret = new char[raw.Length];
			for (int i = 0; i < raw.Length; i++) {
				char cur = raw[i];
				bool is_space = char.IsWhiteSpace(cur);
				if (delete && is_space)
					continue;

				if (is_space && i == raw.Length - 1)
					continue;

				delete = is_space;
				ret[pos] = cur;
				pos++;
			}

			if (pos < ret.Length)
				Array.Resize(ref ret, pos);

			return new string(ret);
		}

	}
}