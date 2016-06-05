using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Collections.Generic;
using HtmlAgilityPack;
using Newtonsoft.Json;

namespace ModIndexer
{
	/*
		Indev = 9,
		Mods = 11,
		Old = 13,
		Games = 15
	*/

	class Program
	{
		static void Main(string[] args)
		{
			Console.Write("Select a mode:\n" +
					"\n\t1) MR titles" +
					"\n\t2) MR topics" +
					"\n\t3) WIP titles" +
					"\n\t4) WIP topics" +
					"\n\t5) Game topics" +
					"\n\t6) Old Mods titles" +
					"\n\t7) Exit" +
					"\n\nYour choice: ");

			ConsoleKeyInfo k = new ConsoleKeyInfo();
			while (k.Key != ConsoleKey.D1 &&
				k.Key != ConsoleKey.D2 &&
				k.Key != ConsoleKey.D3 &&
				k.Key != ConsoleKey.D4 &&
				k.Key != ConsoleKey.D5 &&
				k.Key != ConsoleKey.D6) {
				k = Console.ReadKey(true);

				if (k.Key == ConsoleKey.D7) return;
			}
			Console.WriteLine(k.KeyChar + "\n");

			byte forum = 11;
			bool fetch_topics = false;
			if (k.Key == ConsoleKey.D2)
				fetch_topics = true;

			if (k.Key == ConsoleKey.D3)
				forum = 9;

			if (k.Key == ConsoleKey.D4) {
				forum = 9;
				fetch_topics = true;
			}
			if (k.Key == ConsoleKey.D5) {
				forum = 15;
				fetch_topics = true;
			}
			if (k.Key == ConsoleKey.D6)
				forum = 13;

			Console.Write("Start page: ");
			string start = Console.ReadLine();
			Console.Write("End page: ");
			string stop = Console.ReadLine();

			new Engine(forum, start, stop, fetch_topics);

			Console.WriteLine("=== DONE!\nPress any key to exit.");
			Console.ReadKey(false);
		}
	}

	/* Database types
	 1		mod
	 2		modpack
	 3		indev mod
	 4		indev modpack
	 5		old mod
	 6		game
	 */

	class ForumData
	{
		public int topicId, userId, type;
		public string title, userName, link;

		public ForumData(int _topicId, string _title, int _type, int _userId, string _userName, string _link)
		{
			topicId = _topicId;
			title = _title;
			type = _type;
			userId = _userId;
			userName = _userName;
			link = _link;
		}
	}

	class Engine
	{
		System.Text.Encoding enc = System.Text.Encoding.UTF8;
		List<ForumData> update_data;
		WebClient cli = new WebClient();

		bool fetch_topics;

		public Engine(int forum, string start, string stop, bool _fetch_topics)
		{
			update_data = new List<ForumData>();
			ServicePointManager.ServerCertificateValidationCallback += ValidateRemoteCertificate;

			fetch_topics = _fetch_topics;
			int begin, end;
			if (!int.TryParse(start, out begin))
				begin = 1;

			if (!int.TryParse(stop, out end))
				end = 1;

			for (int i = begin - 1; i < end; i++)
				Start(i, forum);

			byte[] answer = Config.Upload(ref update_data);

			try {
				int[] topics = JsonConvert.DeserializeObject<int[]>(enc.GetString(answer));

				for (int i = 0; i < topics.Length; i++) {
					foreach (ForumData d in update_data) {
						if (d.topicId == topics[i]) {
							Console.WriteLine("\t" + d.title);
							break;
						}
					}
				}
			} catch (Exception e) {
				Console.WriteLine(e.ToString());
				Console.WriteLine(enc.GetString(answer));
			}

			Console.WriteLine("Done.");
		}

		bool ValidateRemoteCertificate(object sender, System.Security.Cryptography.X509Certificates.X509Certificate cert, System.Security.Cryptography.X509Certificates.X509Chain chain, System.Net.Security.SslPolicyErrors error)
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
		void Start(int page, int forum)
		{
			Console.WriteLine("=== Forum " + forum + " - Page " + (page + 1));

			HtmlNodeCollection bodyNode = OpenPage(
				"https://forum.minetest.net/viewforum.php?f=" + forum + "&start=" + (page * 30),
				"//ul[@class='topiclist topics']//dt/a");

			if (bodyNode == null) return;

			bool canAdd = false;
			string title = "",
				author = "";
			int topicId = 0,
				authorId = 0;
			foreach (HtmlNode dtNode in bodyNode) {
				#region extract IDs
				string link = dtNode.GetAttributeValue("href", "");
				link = link.Replace("&amp;", "&");
				string[] linkArgs = link.Split('&');
				if (!canAdd) {
					for (int i = 0; i < linkArgs.Length; i++) {
						if (linkArgs[i].Length < 3) continue;

						string first = linkArgs[i][0].ToString() + linkArgs[i][1];
						if (first == "t=") {
							topicId = int.Parse(linkArgs[i].Remove(0, 2));
						}
					}
					title = dtNode.InnerText;
				} else {
					for (int i = 0; i < linkArgs.Length; i++) {
						if (linkArgs[i].Length < 3) continue;

						string first = linkArgs[i][0].ToString() + linkArgs[i][1];
						if (first == "u=") {
							authorId = int.Parse(linkArgs[i].Remove(0, 2));
						}
					}
					author = dtNode.InnerText;
				}
				#endregion
				if (canAdd && title.Length > 10) {
					#region filter
					byte type = 0; //not a mod
					string lowTitle = title.ToLower();
					if (lowTitle.StartsWith("[wip]")) {
						bool end_space = (title[5] == ' ');
						lowTitle = lowTitle.Remove(0, end_space ? 6 : 5);
						title = title.Remove(0, end_space ? 6 : 5);
					}
					if (lowTitle.StartsWith("[mod]")) {
						title = title.Remove(0, 5);
						type = 1;
					} else if (lowTitle.StartsWith("[modpack]")) {
						title = title.Remove(0, 9);
						type = 2;
					} else if (lowTitle.StartsWith("[game]") &&
							forum == 15) {
						title = title.Remove(0, 6);
						type = 6;
					}
					if (type == 0) {
						canAdd = !canAdd;
						continue;
					}
					switch (forum) {
					case 9: type += 2; break; // make indev
					case 13: type = 5; break; // is old
					}
					#endregion
					string mod_name = null;
					title = specCharsToHex(removeTrash(title, out mod_name));
					string download = "";
					if (fetch_topics && type != 5) {
						bool is_git = getLink(topicId, author, mod_name, ref download);
						if (!(type <= 2 || type == 6 || is_git))
							download = null;
					}
					update_data.Add(new ForumData(topicId, title, type, authorId, author, download));
					author = "";
					title = "";
				}

				canAdd = !canAdd;
			}
		}

		// Analyze topic contents and get link
		bool getLink(int topicId, string author, string mod_name, ref string link)
		{
			Console.WriteLine("=== Topic " + topicId);
			Thread.Sleep(200);
			link = "";

			HtmlNodeCollection bodyNode = OpenPage(
				"https://forum.minetest.net/viewtopic.php?t=" + topicId,
				"//div[@class='content']");

			if (bodyNode == null)
				return false;
			HtmlNodeCollection content = bodyNode[0].SelectNodes(".//a[@class='postlink']");

			if (content == null)
				return false;

			string download = "", source = "";
			int forum_download = 0;

			foreach (HtmlNode dtNode in content) {
				string url = dtNode.GetAttributeValue("href", "");
				string text = dtNode.InnerText;

				if (url.EndsWith(".git")) {
					source = url;
					continue;
				}
				if (url[url.Length - 1] == '/') {
					url = url.Remove(url.Length - 1);
				}

				if (url.StartsWith("./download/file.php?id=")) {
					int pos = 23;
					int number = 0;
					while (pos < url.Length) {
						char cur = url[pos];
						if (cur < 48 || cur > 57)
							break;

						number = number * 10 + (cur - 48);
						pos++;
					}

					string text_lower = text.ToLower().Replace('-', '_');
					if (text_lower.Contains(mod_name) && 
							number > forum_download) {

						forum_download = number;
					}
				} else if (url.Contains(".zip") ||
					url.Contains("/zipball/") ||
					url.Contains("/tarball/") ||
					url.Contains("/archive/") ||
					url.Contains("mediafire.com/")) {
					// Direct download link

					if (url.Contains("://ompldr.org"))
						continue;

					bool contains_git = url.Contains("git");
					if (contains_git) {
						byte count = 0,
							pos = 0;
						for (byte i = 0; i < url.Length; i++) {
							if (url[i] == '/') {
								if (count == 4)
									pos = i;
								
								count++;
							}
						}
						if (count == 6) {
							source = url.Substring(0, pos);

							// Try to find another link if it's not contained in the name
							string src_lower = source.ToLower().Replace('-', '_');
							if (src_lower.Contains(mod_name) || 
									(src_lower.Contains(author.ToLower()) && source == ""))
								break;
						}
					}
					if (download == "")
						download = url;
				} else if (url.Contains("://github.com/") ||
						url.Contains("://notabug.org/")) {
					if (url.Contains("/minetest/minetest") ||
						url.Contains("/commits"))
						continue;

					byte count = 0,
						pos = 0;
					for (byte i = 0; i < url.Length; i++) {
						if (url[i] == '/') {
							if (count == 4) {
								// If it's too long
								pos = i;
							}
							count++;
						}
					}
					if (count < 4 || count > 5)
						continue;

					// url.EndsWith("/tree") || url.EndsWith("/master")
					// //github/user/proj/master
					if (count == 5)
						source = url.Substring(0, pos);
					else
						source = url;

					string src_lower = source.ToLower().Replace('-', '_');
					if (src_lower.Contains(mod_name) || 
							(src_lower.Contains(author.ToLower()) && source == ""))
						break;
				}
			}
			if (source == "" && 
				download == "" && 
				forum_download == 0)
				return false;

			link = source != "" ? source : download;

			if (link == "" && forum_download > 0)
				link = "https://forum.minetest.net/download/file.php?id=" + forum_download;
			return source != "";
		}

		// Remove common control characters and spaces
		string FastTrim(string t, string rm)
		{
			int index = 0;
			bool space_begin = true;
			char[] ret = new char[t.Length];

			for (int i = 0; i < t.Length; i++) {
				char cur = t[i];
				bool found = false;

				if (space_begin) {
					space_begin = (
						cur == ' ' ||
						cur == '\t' ||
						cur == '\n' ||
						cur == '\r'
					);
					found = space_begin;
				}
				for (int k = 0; k < rm.Length && !found; k++) {
					if (rm[k] == cur) {
						found = true;
						break;
					}
				}

				if (!found) {
					ret[index] = cur;
					index++;
				}
			}
			if (index < t.Length)
				Array.Resize(ref ret, index);

			return new string(ret);
		}

		// Remove useless tags from the forum titles
		string bad_chars = ". !?";
		string[] bad_prefix = { "minetest", "mod", "mods" };
		string removeTrash(string t, out string mod_name)
		{
			mod_name = "<unknown>";
			string raw = t;
			int pos = -1,
				open_pos = 0;
			bool opened = false,
				delete = false;
			for (int i = 0; i < t.Length; i++) {
				pos++;
				char cur = t[i];

				if (cur == '[' || cur == '{') {
					opened = true;
					open_pos = pos;
					continue;
				}
				if (cur == ']' || cur == '}') {
					opened = false;

					int len = pos - open_pos + 1;
					string content = raw.Substring(open_pos + 1, len - 2).ToLower();
					int num = 0xC0FFEE;
					bool is_number = int.TryParse(content, out num);

					if (delete || (num != 0xC0FFEE && is_number)
						|| content == "git"
						|| content == "github"
						|| content == "wip"
						|| content == "beta") {
						raw = raw.Remove(open_pos, len);
						pos -= len;
					} else {
						content = content.ToLower().Replace('-', '_');
						int start_substr = 0;

						foreach (string prefix in bad_prefix) {
							if (content.Length <= start_substr + prefix.Length + 1)
								break;

							if (content.Substring(start_substr, prefix.Length) == prefix)
								start_substr += prefix.Length;
							
							if (content[start_substr] == '_')
								start_substr++;
						}
						
						mod_name = content.Substring(start_substr);
					}
					delete = false;
					continue;
				}
				if (opened &&
					!delete &&
					strContains(bad_chars, cur)) {
					delete = true;
				}
			}

			delete = true;
			pos = 0;
			char[] ret = new char[raw.Length];
			for (int i = 0; i < raw.Length; i++) {
				char cur = raw[i];
				bool is_space = (cur == ' ');
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

		// Convert special characters to HTML code
		string specCharsToHex(string t)
		{
			char[] fromCr = { '"', '\'', '\\', '{', '}', '|', '%', ':', '<', '>' };
			string[] toStr = { "&quot;", "&#39;", "&#92;", "&#123;", "&#125;", "&#124;", "&#37;", "&#58;", "&lt;", "&gt;" };
			System.Text.StringBuilder sb = new System.Text.StringBuilder();

			bool wasSpace = false;
			for (int i = 0; i < t.Length; i++) {
				bool isSpace = (t[i] == ' ');
				if (wasSpace && isSpace)
					continue;
				// Cut off non-ASCII
				if ((ushort)t[i] > 0xFF)
					continue;

				bool found = false;
				for (int k = 0; k < fromCr.Length; k++) {
					if (t[i] == fromCr[k]) {
						sb.Append(toStr[k]);
						found = true;
						break;
					}
				}

				wasSpace = isSpace;
				if (!found)
					sb.Append(t[i]);
			}

			return sb.ToString();
		}

		bool strContains(string t, char c)
		{
			for (int i = 0; i < t.Length; i++) {
				if (t[i] == c)
					return true;
			}
			return false;
		}
	}
}